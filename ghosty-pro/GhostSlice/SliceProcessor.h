#pragma once
// GHOST SLICE processor: owns the project (History of immutable snapshots), the loaded source, the compiled plan, the STUDIO
// render cache and the real-time Player. Message thread: every edit goes through apply(); the audio thread only reads the
// published PlayTable. Output: Player -> CleanStage ceiling (true peak, fixed latency) -> demo drop-out.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "AnalysisAlign.h"
#include "Play.h"
#include "Hold.h"
#include "Repeat.h"
#include "ProjectIO.h"
#include "Export.h"
#include "License.h"
#include "ExportGate.h"
#include "CleanStage.h"
#include "SyncTransport.h"
#include "GhostThemes.h"

class SliceProcessor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::Timer
{
public:
    SliceProcessor();
    ~SliceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghost Slice"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---------------------------------------------------------------- message thread API
    using Project = gslice::Project;
    const Project& project() const { return history.current(); }
    // apply an edit (one undo step; inside a gesture the steps merge). Returns false when p equals the current project.
    bool apply (const Project& p, const juce::String& label);
    void beginGesture() { history.beginGesture(); }
    void endGesture() { history.endGesture(); }
    bool undo();
    bool redo();
    gslice::History& getHistory() { return history; }

    // audio in: a file -> decode on a worker, then install (onLoaded on the message thread). Sync version for tests.
    void loadAudioAsync (const juce::File& f);
    bool loadAudio (const juce::File& f, juce::String& error, bool keepProject = false);
    bool loadSourceForTests (std::shared_ptr<const gslice::SourceAudio> s);   // memory audio (tests)
    bool isLoading() const { return loading.load(); }
    std::shared_ptr<const gslice::SourceAudio> getSource() const { return source; }
    std::shared_ptr<const gslice::WorkingAudio> getWorking() const { return work; }
    juce::File audioFile() const { return source != nullptr ? source->file : juce::File(); }

    // projects (the plugin's own format; allowed in the demo)
    bool saveProject (const juce::File& location, juce::String& error);
    bool openProject (const juce::File& location, juce::String& error);   // false + error "relink" when the audio is missing
    bool relinkAudio (const juce::File& f, juce::String& error);           // the missing audio, picked by the user
    bool needsRelink() const { return relinkPending; }
    juce::File projectLocation() const { return projectLoc; }

    // the plan the player is using (compiled from the current project at the host rate)
    const gslice::Plan& currentPlan() const { return plan; }
    double hostRate() const { return hostSr.load(); }
    double pfToHf (double pf) const { return pf * hostSr.load() / project().sampleRate; }
    double hfToPf (double hf) const { return hf * project().sampleRate / hostSr.load(); }

    // analysis (phases 7 / 13): onsets of the project's material profile, tempo, pitch track, notes, key - on a background
    // thread, cached under the project's (or the loose) cache folder. null until the current source's result lands.
    std::shared_ptr<const gslice::analysis::Result> analysis() const;
    gslice::analysis::Material material() const { return gslice::analysis::materialFromName (project().material); }
    bool analysing() const { return analyzer.busy(); }
    bool waitAnalysis (int timeoutMs) { const bool ok = analyzer.waitIdle (timeoutMs); handleAsyncUpdate(); return ok; }
    void reanalyse();
    // the events ALIGN works on: the analysis' onsets of the material (or the source's AUTO onset list until it lands)
    std::vector<gslice::analysis::Event> alignEvents() const;

    // QUALITY: LIVE (stretch in real time) / STUDIO (background renders replace LIVE at safe points)
    void setStudioMode (bool on);
    bool studioMode() const { return useStudio; }
    size_t studioPending() const { return studio.pending(); }
    bool waitStudio (int timeoutMs) { const bool ok = studio.waitIdle (timeoutMs); handleAsyncUpdate(); return ok; }
    gslice::StudioCache& studioCache() { return studio; }
    float studioProgress() const { return studio.progress(); }
    // OFFLINE BOUNCE: in a non-realtime render (isNonRealtime) each block waits until the STUDIO renders it plays are ready, up to
    // offlineWaitMs per block (then that table plays LIVE where it must; the next edit tries again)
    int offlineWaitMs = 20000;
    std::atomic<int> offlineWaits { 0 }, offlineTimeouts { 0 };
    // A/B: B = the original source at 1x
    void setCompareOriginal (bool b);
    bool compareOriginal() const { return abOriginal; }

    // export (call only after ghosty::exportAllowed said yes; these re-check the licence silently)
    juce::AudioBuffer<float> renderArrangement();                           // STUDIO render of the plan (with the ceiling)
    bool exportWav (const juce::File& f, juce::String& error);
    bool exportMidi (const juce::File& f, juce::String& error);

    // ---------------------------------------------------------------- transport (audition; the DAW drives it while it plays)
    void play (bool on);
    bool isPlaying() const { return ownPlaying.load() || hostPlaying.load(); }
    bool hostIsPlaying() const { return hostPlaying.load(); }
    void setCursor (gslice::int64 hf) { cursorHf.store (juce::jmax<gslice::int64> (0, hf)); if (! isPlaying()) playHf.store (cursorHf.load()); else seekReq.store (cursorHf.load()); }
    gslice::int64 cursor() const { return cursorHf.load(); }
    gslice::int64 playhead() const { return displayHf.load(); }
    void setLoop (gslice::int64 a, gslice::int64 b, bool on) { loopA.store (a); loopB.store (b); loopOn.store (on && b > a + 64); }
    bool looping() const { return loopOn.load(); }
    std::pair<gslice::int64, gslice::int64> loopRange() const { return { loopA.load(), loopB.load() }; }
    bool isHosted() const { return wrapperType != juce::AudioProcessor::wrapperType_Standalone; }

    ghosty::License license { "Ghost Slice" };
    ghosty::DemoGate demo;
    ghosty::SyncTransport sync;
    std::atomic<int> uiTheme { 0 };          // per-project look (session state)
    juce::String lastMessage;                // short status for the info strip
    std::function<void()> onChanged;         // the editor repaints (message thread)
    std::function<void (const juce::String&)> onToast;
    int ceilingLatency() const { return ceiling.latency(); }
    gslice::Player& getPlayer() { return player; }
    // after UNLOCK: a host session that was not restored in the demo comes back
    void unlockedNow();

    // ---------------------------------------------------------------- PLAY (phase 10)
    gslice::play::Engine& getPlayEngine() { return playEngine; }
    std::shared_ptr<const gslice::play::Bank> getBank() const { return bank; }
    std::vector<gslice::play::Slice> currentSlices() const;                // the bank's (snapped) slices, else the plan's
    bool bankPending() const { return bankWant.isNotEmpty() && (bank == nullptr || bank->key != bankWant); }
    bool buildBankNow();                                                   // synchronous (tests, export)
    void audition (int note, float velocity, bool on) { playEngine.audition (note, velocity, on); }
    bool exportPlayMidi (const juce::File& f, juce::String& error);        // the mapping as MIDI (gate: slice.export.playmidi)
    bool writeSliceWav (int slice, const juce::File& f, juce::String& error);   // one pad's slice (gate: slice.drag.padwav)
    // REPEAT tempo: the DAW's while it drives the transport (SYNC: DAW in a host), else the project's
    double repeatBpm() const;

private:
    void handleAsyncUpdate() override;       // studio renders arrived / host rate changed: republish
    void timerCallback() override;           // debounced STUDIO job requests
    void rebuild (bool recompile);
    void installSource (std::shared_ptr<const gslice::SourceAudio> s, bool keepProject);
    void publishTable();
    void requestStudio();
    void publishPlay();
    void requestBank();
    void offlineWait (gslice::int64 T, int n);
    gslice::uint64 offlineGaveUpGen = 0;

    gslice::History history;
    std::shared_ptr<const gslice::SourceAudio> source;
    std::shared_ptr<const gslice::WorkingAudio> work;
    gslice::Plan plan, planOriginal;
    gslice::StudioCache studio;
    gslice::analysis::Analyzer analyzer;
    std::atomic<bool> analysisArrived { false };
    juce::String analysedKey;                // fingerprint + material of the running / last analysis
    gslice::Player player;
    ghosty::clean::Ceiling ceiling;
    bool useStudio = true, abOriginal = false, relinkPending = false;
    juce::File projectLoc;
    juce::MemoryBlock pendingState;          // a session restored while locked (the demo reopens empty)
    std::atomic<double> hostSr { 48000.0 }, preparedSr { 0.0 };
    std::atomic<bool> loading { false }, ownPlaying { false }, hostPlaying { false }, loopOn { false };
    std::atomic<gslice::int64> playHf { 0 }, cursorHf { 0 }, displayHf { 0 }, loopA { 0 }, loopB { 0 }, seekReq { -1 }, planLenHf { 0 };
    std::atomic<bool> stopReq { false };
    int maxBlock = 512;
    gslice::play::Engine playEngine;
    std::shared_ptr<const gslice::play::Bank> bank;
    juce::String bankWant;
    std::shared_ptr<std::atomic<bool>> bankCancel;
    std::atomic<double> hostBpm { 0.0 }, compiledBpmA { 0.0 }, projectBpm { 120.0 };
    std::atomic<bool> hasRepeats { false };
    double compiledBpm = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceProcessor)
};
