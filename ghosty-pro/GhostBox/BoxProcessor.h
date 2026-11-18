#pragma once
// Ghost Box: the GhostBlock FX rack as an effect for any track. The rack itself (gpfx::Rack, GhostyPro/fx/) is
// shared with Ghosty Pro; this build compiles it with GPFX_BOX=1, which adds RETRO, FATTEN, CRACKLE and HAUNTED HALLS.
#include <juce_audio_processors/juce_audio_processors.h>
#include "ProFx.h"
#include "FxCore.h"
#include "FxBoxUnits2.h"   // LatentDry: the latency-following dry paths
#include "License.h"
#include "Bypass.h"
#include "SnapshotBank.h"
#include "BoxScenes.h"
#include "BoxCapture.h"
#include "BoxDna.h"
#include "BoxFocus.h"
#include "BoxPrint.h"
#include "TempoMul.h"   // TEMPO  ½x | 1x | 2x (shared with the other GhostBlock plugins)
#include <atomic>

namespace boxlayers { class Engine; }

class BoxProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BoxProcessor();
    ~BoxProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghost Box"; }
    bool acceptsMidi() const override { return true; }    // GHOST SCENES: MIDI keys trigger the scenes (C1 .. G1 by default)
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    // BYPASS (shared GhostBlock behaviour, Shared/Bypass.h): the unprocessed input, time-aligned with the processed path, 10 ms crossfade.
    // The rack keeps running while bypassed so tails and state continue. Never touched by scenes / presets / MORPH / BRAIN / macros / A/B.
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }
    juce::AudioParameterBool* bypassParam = nullptr;

    juce::ValueTree stateTree();
    void applyStateTree (juce::ValueTree, bool hostRestore = true);   // hostRestore: snap smoothers (project load); false: ducked live change (A/B)
    // A/B compare of the WHOLE box (every unit, order, patterns, box_ controls). Message thread. Undoable.
    int abCurrent() const { return abSlot; }
    bool abHas (int which) const { return abTree[juce::jlimit (0, 1, which)].isValid(); }
    void abToggle();            // store the current state in the current slot, switch to the other (first time: a copy of this one)
    void abCopyToOther();       // the other slot becomes a copy of what you hear
    // SNAPSHOT (Shared/SnapshotBank.h): one click saves the whole rack sound (units, settings, order, macros, MORPH, MOTION) into the
    // SNAPSHOTS bank; never the look, BYPASS or the SPIRIT BOARD. Restoring is the click-free live load (the A/B path).
    std::unique_ptr<ghosty::SnapshotBank> snapshots;
    juce::ValueTree captureSound();
    void restoreSound (const juce::ValueTree& t);

    // GHOST SCENES (GhostBox/BoxScenes.h = the slots + settings, GhostyPro/fx/FxBoxScenes.h = the audio side). Message thread.
    boxscenes::Bank sceneBank;
    gpfx::SceneEngine& sceneEngine();
    juce::ValueTree sceneSound();                                // what a scene stores: captureSound() without IN / OUT / FIXED LATENCY
    void sceneStore (int i);                                     // what you hear -> slot i
    void sceneClear (int i);
    void sceneRename (int i, const juce::String& name);
    void sceneCopy (int from, int to);
    bool sceneUndo();                                            // the last slot edit
    int sceneFillStarters();                                     // empty slots get starter scenes (built from the factory rack scenes); returns how many
    void sceneTrigger (int i) { sceneEngine().post (0, i); }     // a click: the scene stays (latch)
    void scenePress (int i) { sceneEngine().post (1, i); }       // a press: the scene while held ...
    void sceneRelease (int i) { sceneEngine().post (2, i); }     // ... released after a long press: back to what played before
    void sceneClickUp (int i) { sceneEngine().post (3, i); }     // ... released quickly: it stays
    void sceneSettingsChanged();                                 // after editing sceneBank.set
    void scenesChanged();                                        // after slot edits: publish the targets to the audio side
    void sceneCommitNow();                                       // writes a landed scene into the host parameters (the 50 Hz scene timer; tests)
    std::function<void()> onScenesChanged;                       // the scene bar refreshes
    static bool notInScene (const juce::String& id);

    // GHOST CAPTURE (GhostBox/BoxCapture.h): the rolling history of the processed output + the shelf of the last 8 captures. Message thread.
    boxcapture::History history;
    std::vector<std::shared_ptr<boxcapture::Clip>> shelf;        // newest first, at most boxcapture::kShelf
    std::atomic<int> shelfVersion { 0 };                         // bumped on every shelf change (the CAPTURE page rebuilds its tiles)
    int captureBarsIdx = 1;                                      // 2 / 4 / 8 / 16 bars (default 4)
    std::atomic<double> lastBpm { 120.0 }, lastBeatsPerBar { 4.0 }, lastBarStart { 0.0 }; std::atomic<bool> lastHaveBar { false };
    juce::String captureNow();                                   // the last N bars onto the shelf; returns what to tell the user
    std::shared_ptr<boxcapture::Clip> clipById (int id) const;
    void clipReverse (int id);
    void clipRemove (int id);
    void clipPreview (int id, bool on);                          // LOOP preview through the plugin's output (one at a time)
    int previewingId() const { return previewId; }
    float previewPos() const;                                    // 0..1 (the tile's play head)
    bool clipDrag (int id, juce::Component* ui);                 // gated: box.capture.drag
    void clipSaveAs (int id, juce::Component* ui);               // gated: box.capture.save
    bool clipSendToSlice (int id, juce::Component* ui);          // gated: box.capture.slice (WAV + .ghostclip.json, both dragged)
    void setHistoryOn (bool on);
    static juce::File captureDir();                              // Documents/Ghosty/Ghost Box/Captures
    std::function<void()> onCaptureRequest;                      // Shift+Alt+C from the window (the editor shows the result)

    // GHOST DNA (GhostBox/BoxDna.h). Message thread; the results are computed (and loudness-matched offline) on a worker thread.
    boxdna::Genome& genome();
    boxdna::Locks dnaLocks; juce::int64 dnaSeed = 1; float dnaStrength = 0.35f, dnaBlend = 0.5f;
    std::array<juce::ValueTree, 4> dnaVariations; std::array<float, 4> dnaTrim {}; std::array<juce::String, 4> dnaNotes; std::array<juce::int64, 4> dnaSeeds {};
    int dnaCount = 0, dnaAuditioning = -1; juce::String dnaMessage;
    std::atomic<int> dnaVersion { 0 }; std::atomic<bool> dnaBusy { false };
    juce::StringArray dnaSourceNames (std::vector<int>& ids);   // WHAT YOU HEAR, the filled scenes, the newest snapshots (id 0 / 1..8 / 100+i)
    juce::ValueTree dnaSource (int id);
    void dnaMutate();                                            // what you hear (or the original while auditioning) mutated at SEED / STRENGTH, heard at once
    void dnaBreed (int a, int b);                                // A x B at SEED / BLEND, heard at once
    void dnaEvolve();                                            // 4 mutations (SEED .. SEED+3) to audition
    void dnaAudition (int k);
    void dnaKeep();
    void dnaUndo();                                              // UNDO TO ORIGINAL
    bool dnaOriginalValid() const { return dnaOriginal.isValid(); }
    bool dnaWait (int timeoutMs = 30000);                        // tests: until the worker's results arrived
    bool unitIsOn (int u) const { auto* v = apvts.getRawParameterValue (gpfx::pid (u, "on")); return v != nullptr && v->load() > 0.5f; }
    static float loudnessOf (const juce::ValueTree& sound, double rate);   // offline K-weighted loudness of the DNA test signal through a sound
    static juce::ValueTree gainMatched (const juce::ValueTree& original, float lOriginal, juce::ValueTree result, double rate, float* trimDb = nullptr, float* lFinal = nullptr);

    // GHOST FOCUS (GhostBox/BoxFocus.h): an intention -> 4 starting chains from the rack's units + factory presets, loudness-matched (the
    // DNA matcher) on the worker thread to what played before. Message thread. Session only (never saved, never in sounds / scenes).
    int focusIntent = -1, focusAuditioning = -1; float focusAmount = 1.0f;
    std::array<juce::ValueTree, boxfocus::kChains> focusVariants; std::array<float, boxfocus::kChains> focusTrim {};
    juce::String focusMessage; std::atomic<int> focusVersion { 0 }; std::atomic<bool> focusBusy { false };
    int focusCount() const { int n = 0; for (auto& v : focusVariants) if (v.isValid()) ++n; return n; }
    juce::ValueTree focusChainSound (int intent, int k, const juce::ValueTree& original);   // the chain as a sound, built on a private rack (not matched)
    void focusPick (int intent);                                  // builds the 4 chains, matches their loudness on the worker (focusVersion bumps when ready)
    void focusAudition (int k);                                   // hear chain k (the click-free live load; one UNDO step)
    void focusSetAmount (float a);                                // refine: the chain's units' wet MIX x a (live)
    void focusKeep();                                             // the chain stays (the next FOCUS starts from it)
    void focusBack();                                             // back to exactly what played before the first FOCUS chain
    bool focusOriginalValid() const { return focusOriginal.isValid(); }
    bool focusWait (int timeoutMs = 60000);                       // tests: until the worker's results arrived

    // GHOSTPRINT (GhostBox/BoxPrint.h): a short reference (a file or a CAPTURE clip) -> its gesture as GHOST MOTION timeline lanes.
    // Message thread; the reading + analysis run on a worker thread. Session only (the reference and the analysis are never saved;
    // the lanes APPLY writes are part of the timeline, so they are).
    boxprint::Analysis printResult; boxprint::Settings printSet; boxprint::Options printOpt;   // printOpt: BPM (0 = detect) / BARS (0 = AUTO)
    juce::String printMessage, printName; std::atomic<int> printVersion { 0 }; std::atomic<bool> printBusy { false };
    void printLoadFile (const juce::File& f);                     // WAV / AIFF / FLAC / MP3 / OGG; a bad file gives a message, never a crash
    void printLoadClip (int clipId);                              // a GHOST CAPTURE clip (its tempo and bars are known)
    void printLoadBuffer (std::shared_ptr<const juce::AudioBuffer<float>> audio, double rate, const juce::String& name, boxprint::Options opt);
    void printReanalyse();                                        // after BPM / BARS changed
    boxprint::Pattern printPattern() const { return boxprint::makePattern (printResult, printSet); }
    int printApply();                                             // writes the lanes (one UNDO step); returns how many
    bool printWait (int timeoutMs = 30000);                       // tests: until the worker's result arrived

    juce::AudioProcessorValueTreeState apvts;
    gpfx::Rack rack;   // after apvts: destroyed first, so the rack's worker threads stop before the parameters go away
    ghosty::License license { "Ghost Box" };
    ghosty::DemoGate demo;
    std::atomic<float> inPeak[2] {}, outPeak[2] {};   // meters (display only)
    juce::ValueTree abTree[2]; int abSlot = 0;
    // the window's look (ghosty::themes: 0 BLACK, 1 PURPLE, 2 LIGHT; 3 GHOST = Ghost Box's own glass look): a per-project UI preference (session state only; never in presets / scenes / A-B)
    // TEMPO  ½x | 1x | 2x (Shared/TempoMul.h): every tempo-synced thing in the box runs at the host's tempo x this (the host's tempo AND song
    // position are scaled before the rack sees them, so it stays locked to the DAW's bars). Default 1x = exactly as before. Saved with the
    // project (only when not 1x); never part of a sound (snapshots, A/B, scenes, DNA leave it alone).
    std::atomic<int> tempoMul { ghosty::tempomul::Normal };
    double tempoFactor() const { return ghosty::tempomul::factor (tempoMul.load (std::memory_order_relaxed)); }
    std::atomic<double> hostBpm { 120.0 };          // the DAW's own tempo (the TEMPO switch shows each choice's BPM)
    std::atomic<int> uiTheme { 0 };                 // 3 = GHOST (Ghost Box's 4th look, liquid glass on LIGHT; saved as "ghost")
    static constexpr int kLookGhost = 3;
    // SPIRIT BOARD LAYER mode (GhostBox/BoxLayers.h): every weighted ghost runs its own rack on the same input, mixed at the weights
    std::unique_ptr<boxlayers::Engine> layers;
    void syncLayers();   // message thread: after board / mode changes (also from the 10 Hz timer)
    bool waitLayersReady (int timeoutMs = 20000);   // tests: until the background-built LAYER racks are published

private:
    void timerCallback() override;
    void processSlice (juce::AudioBuffer<float>& buffer, int inCh, int outCh, const juce::MidiBuffer& midi, int midiOffset);   // one slice of at most the prepared block size
    struct SceneTimer : juce::Timer { BoxProcessor& p; explicit SceneTimer (BoxProcessor& o) : p (o) {} void timerCallback() override { p.sceneCommitNow(); } } sceneTimer { *this };
    juce::ValueTree sceneReturnState;   // a held scene: the sound before it (restored by the return's commit)
    // GHOST CAPTURE preview: the audio thread reads the published buffer (kept alive here until it has left it)
    std::atomic<const juce::AudioBuffer<float>*> previewBuf { nullptr }; int previewId = -1; juce::int64 previewAt = 0; std::atomic<float> previewFrac { 0 };
    const juce::AudioBuffer<float>* previewSeen = nullptr;
    std::vector<std::pair<std::shared_ptr<const juce::AudioBuffer<float>>, juce::int64>> previewKeep;
    std::shared_ptr<const juce::AudioBuffer<float>> previewHold;
    std::atomic<juce::int64> audioBlocks { 0 };
    void retirePreview();
    std::unique_ptr<juce::FileChooser> captureChooser;
    int nextClipId = 1;
    // GHOST DNA worker
    struct DnaJob { juce::ValueTree original; double rate = 48000; juce::int64 seed = 1; float strength = 0.35f, blend = 0.5f; boxdna::Locks locks; bool evolve = false, applyNow = false;
                    std::vector<juce::ValueTree> results; std::vector<float> trims; std::vector<juce::String> notes; std::vector<juce::int64> seeds; juce::String message; };
    std::unique_ptr<boxdna::Genome> dnaGenome;
    juce::ValueTree dnaOriginal;
    std::shared_ptr<bool> dnaAlive = std::make_shared<bool> (true);
    void dnaStart (bool evolve, std::function<void (DnaJob&)> work);
    void dnaFinish (DnaJob& job);
    juce::ValueTree dnaResult (const DnaJob& j, juce::int64 seed, float lOrig, std::function<juce::ValueTree (juce::int64)> make, float& trim, juce::int64& used);
    std::shared_ptr<const juce::AudioBuffer<float>> printRef; double printRefRate = 48000; std::shared_ptr<bool> printAlive = std::make_shared<bool> (true);
    std::atomic<bool> printCancel { false }; int printTicket = 0;
    void printStart (std::function<void (boxprint::Analysis&, juce::String&)> work);
    juce::ThreadPool printPool { 1 };
    juce::ThreadPool dnaPool { 1 };   // (last: its jobs stop before anything they use goes away)
    void applySceneSound (const juce::ValueTree& t);
    juce::ValueTree focusOriginal;   // GHOST FOCUS: what played before the first chain (BACK TO ORIGINAL)
    void focusApplyAmount();
    int sceneMirrorSeq = -1;
    std::atomic<float>* inGainP = nullptr; std::atomic<float>* outGainP = nullptr; std::atomic<float>* mixP = nullptr; std::atomic<float>* bypassP = nullptr;
    juce::AudioBuffer<float> dryBuf, scBuf, rawBuf; gpfx::LatentDry dryDelay, rawDelay; int dryDelayLen = -1, maxBlock = 512;
    ghosty::BypassFader bypassFader;
    float lastIn = -1, lastOut = -1, mixNow = 1; double sr = 48000, freePpq = 0;
    std::atomic<int> latencyNow { 0 };
    std::atomic<double> lastBlockMs { -1.0 }, lastLoudMs { -1.0 };   // "is audio playing right now" for host restores (live = ducked, else snapped)
    bool audioIsLive() const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoxProcessor)
};
