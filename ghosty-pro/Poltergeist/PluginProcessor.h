#pragma once
#include "SnapshotBank.h"
#include "License.h"
#include "ExportGate.h"
#include "Engine.h"
#include "Summon.h"
#include "BassPresets.h"
#include "MidiFx.h"
#include "Chop.h"
#include "VintageCharacter.h"
#include "Bypass.h"
#include "Morph.h"
#ifndef GHOSTY_PROCESS_LAB
 #define GHOSTY_PROCESS_LAB 0     // PROCESS LAB (Shared/ProcessLab*): CMake option GHOSTY_PROCESS_LAB, off until its engine passes its gate
#endif
#if GHOSTY_PROCESS_LAB
 #include "ProcessLab.h"
#endif

class PoltergeistProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    ghosty::License license { "Poltergeist" }; ghosty::DemoGate demo;   // paid version: license key + demo mode
    PoltergeistProcessor();
    ~PoltergeistProcessor() override;
    const juce::String getName() const override { return "Poltergeist"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }
    juce::AudioParameterBool* bypassParam = nullptr;
    std::unique_ptr<ghosty::SnapshotBank> snaps;
    int uiTheme = 0;                         // the look (ghosty::themes: 0 BLACK, 1 PURPLE, 2 LIGHT); saved as theme_name
    // EXPORT gate (demo mode = no drag-out / file export; Shared/ExportGate.h, docs/DEMO-EXPORT-GATE.md). allowExport(ui, id) is the
    // user-facing gate (refused: the UNLOCK hint on ui's editor); exportUnlocked() asks silently; exportBackstop() is the silent
    // re-check inside every file writer (counts refusals). testLicense: -1 real licence, 0 locked, 1 unlocked (tests)
    int testLicense = -1; int exportRefused = 0;
    bool exportUnlocked() const { return testLicense != 0 && ghosty::exportUnlocked(license); }
    bool allowExport(juce::Component* ui, const char* id) {
        const bool ok = ghosty::exportAllowed(license, ui, id, testLicense == 0); if (!ok) ++exportRefused; return ok;
    }
    bool exportBackstop() { if (exportUnlocked()) return true; ++exportRefused; return false; }
    // SAVES while LOCKED (demo): presets / user sounds / snapshots / boards / the project stay allowed, but audio the plugin MADE
    // (gs::SampleData::made: RESAMPLE, PROCESS LAB results, Sample Lab renders) is left out; the user's own files stay in.
    // savesKeepMade(): unlocked. leftOut counts what the last save left out; onRenderedLeftOut(id) = a click save left something
    // out (the editor shows the UNLOCK hint).
    bool savesKeepMade() const { return testLicense != 0 && ghosty::exportgate::keepRenderedAudio(license); }
    mutable int leftOut = 0;
    std::function<void(const char* id)> onRenderedLeftOut;
    void tellLeftOut(const char* id) { if (leftOut > 0 && onRenderedLeftOut) onRenderedLeftOut(id); }   // the editor's theme (a per-project UI preference: saved in the state, never in a preset)   // "bypass": mutes the whole plugin (Shared/Bypass.h)
    ghosty::BypassFader bypass;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    // programs: the original presets first (same numbers as before), then the BASS folders
    static int numLegacyPresets() { return (int) gs::factoryPresets().size(); }
    int getNumPrograms() override { return numLegacyPresets() + (int) gs::bassPresets().size(); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int i) override { loadPreset(i); }
    const juce::String getProgramName(int i) override {
        if (!juce::isPositiveAndBelow(i, getNumPrograms())) return {};
        return i < numLegacyPresets() ? juce::String(gs::factoryPresets()[(size_t) i].name) : juce::String(gs::bassPresets()[(size_t) (i - numLegacyPresets())].name);
    }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void restoreState(const void* data, int size);   // file loads (no demo check); setStateInformation = host restore

    // patches
    void applyPatch(const gs::Patch& p);
    juce::String summon(const juce::String& words);   // returns what it made
    void loadPreset(int i);
    // user sounds: the whole plugin state (patch + Sample Ghost + Lab + keys) in Documents/Ghosty/Poltergeist
    juce::File userDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Poltergeist"); d.createDirectory(); return d; }
    bool saveUserSound(const juce::String& name) { juce::MemoryBlock mb; getStateInformation(mb); tellLeftOut("save:polt.user-sound"); patchName = name; return userDir().getChildFile(juce::File::createLegalFileName(name) + ".ghostpatch").replaceWithData(mb.getData(), mb.getSize()); }
    bool loadUserSound(const juce::File& f) { juce::MemoryBlock mb; if (!f.loadFileAsData(mb) || mb.getSize() == 0) return false; const auto keepBoard = morphTree(); restoreState(mb.getData(), (int) mb.getSize()); morphFromTree(keepBoard); patchName = f.getFileNameWithoutExtension(); snapshotsLeft(); return true; }
    // SNAPSHOT (Shared/SnapshotBank.h): the whole sound, exactly what SAVE writes (all parameters, the Sample Ghost and its sample data,
    // the Sample Lab, CHOP, single keys, PROCESS LAB). Loading goes through the same path as LOAD; the look and BYPASS stay as they are.
    ghosty::SnapshotBank& snapshots();
    juce::ValueTree captureSound();
    void restoreSound(const juce::ValueTree& t);
    // ---- MORPH BOARD (message thread; the audio thread blends through morph)
    ghosty::spirit::Board board;
    struct GhostSound { juce::String source; std::vector<float> values; std::shared_ptr<gs::SampleData> sample; };
    std::map<juce::String, GhostSound> ghostSounds;          // by ghost id
    gs::Morph morph;
    int morphAddGhost(float x, float y, const juce::String& source);   // source: "current" | "snap:<file name>" | "preset:<index>"; returns the ghost's index or -1
    bool morphSetGhost(int i, const juce::String& source);
    void morphRemoveGhost(int i);
    void morphPublish();                                     // after every board / content edit
    void morphApplyBlend();                                  // the blend at the puck -> the knobs (MORPH turns off)
    juce::ValueTree morphTree(bool forSave = false) const; void morphFromTree(const juce::ValueTree& t);
    juce::File boardsDir() const { auto d = userDir().getChildFile("Boards"); d.createDirectory(); return d; }
    bool saveBoard(const juce::String& name); bool loadBoard(const juce::File& f);
    bool morphActive() const { return apvts.getRawParameterValue("morph_on")->load() > 0.5f && !board.ghosts.empty(); }
    int morphLeader = -1;
    void morphTimer();
    void snapshotsLeft() { if (snaps) snaps->clearCurrent(); }   // another sound was loaded: no snapshot is "current" any more
    // Sample Ghost
    bool loadSampleFile(const juce::File& f, juce::String& err);   // (older path: also adds the file to the Sample Lab and CHOP data)
    // ADD SAMPLE (the FM page's sample slot, owner 2026-10-26): a WAV / AIFF / FLAC / MP3 becomes the Sample Ghost as an FM SOURCE:
    // classic playback, START 0, the whole file, LOOP on, the ROOT note detected (KEY C3 = the file's own pitch when the root is right).
    // Nothing else changes: the Sample Lab / CHOP data of an old project stay as they are (they keep playing from the engine).
    bool addSampleFile(const juce::File& f, juce::String& err);
    bool addSample(std::shared_ptr<gs::SampleData> s);
    static int detectRoot(const gs::SampleData& s);   // MIDI note of the sample's pitch (YIN, rounded), -1 = no clear pitch
    int detectedRoot = -1;                            // of the sample ADD SAMPLE loaded last (-1: none / no clear pitch)
    int sampleRoot() const;                           // the note that plays the sample at its own pitch (60 - PITCH)
    void setSampleRoot(int note);                     // ROOT override: PITCH = 60 - note (one host gesture)
    bool resampleRecent(double seconds = 4.0);   // the last few seconds of Poltergeist's own output
    void clearSample() { rawSample.reset(); engine.setSample(nullptr); }
    void setSampleGhost(std::shared_ptr<gs::SampleData> s);   // also starts the keep-tempo copies in the background

    // ---- Sample Lab (message thread)
    gs::Lab lab;
    juce::String keyNames[128];
    int labAddFile(const juce::File& f, juce::String& err);
    void labMapKey(int note, std::shared_ptr<gs::SampleData> s);
    void labClearKey(int note);
    void labClearKeys();
    void labRenderPreview();                 // bounce the timeline (background) -> preview + drag-out
#if GHOSTY_PROCESS_LAB
    // ---- PROCESS LAB (Shared/ProcessLab*) inside the SAMPLE LAB: works on the current Lab source
    plab::AuditionPlayer labAudition;        // PLAY SRC / PLAY FX of the panel (rendered at the end of processBlock)
    juce::ValueTree plabState() const { const juce::ScopedLock sl(plabLock); return plabTree.createCopy(); }   // "PLAB": the chain, saved with the project
    void setPlabState(const juce::ValueTree& v) { const juce::ScopedLock sl(plabLock); plabTree = v.createCopy(); ++plabGen; }
    void storePlabState(const juce::ValueTree& v) { const juce::ScopedLock sl(plabLock); plabTree = v.createCopy(); }   // the editor keeps it current
    std::atomic<int> plabGen { 0 };          // bumped when a project brings a chain (the editor re-loads its panel)
    bool replaceLabSource(int id, const plab::Audio& a);   // a committed PROCESS LAB result becomes that source
    int addLabSourceFrom(const plab::Audio& a, const juce::String& suffix, int fromId);
    std::vector<plab::Region> labSlices(int id) const;     // the CHOP slices on Lab source id (when it is the chopped sound), at its own rate   // BOUNCE->NEW: a new Lab source (made current), the original kept
    // the PROCESS LAB works on each source at its own rate and channel count (the Lab's timeline copy is mono 44.1 kHz):
    // kept per Lab source id, saved with the project (16-bit), the timeline copy re-made from it after every commit
    std::map<int, std::shared_ptr<plab::Audio>> labOriginals;
    plab::Audio labProcessAudio(int id) const;             // what the panel gets for source id
    static std::shared_ptr<plab::Audio> originalFromFile(const juce::File& f);   // trimmed / levelled exactly like the Lab's copy
#else
    std::vector<juce::ValueTree> plabKept;   // a project's PROCESS LAB data, kept as-is by a build without it
#endif
    std::shared_ptr<gs::SampleData> labRendered;
    void labPushUndo(const std::vector<gs::LabClip>& before);   // Sample Lab UNDO (clips, flex lines, section pitches)
    bool labUndo(); bool labRedo();
    bool labCanUndo() const { return !labUndoStack.empty(); }
    bool labCanRedo() const { return !labRedoStack.empty(); }
    std::atomic<bool> previewOn { false };
    // SAMPLE LAB transport: PLAY plays YOUR EDIT (the rendered timeline) once from the start, or loops (LOOP on: then locked
    // to the host beat while the host plays, as the old PREVIEW did); ORIGINAL plays the source as it was loaded instead
    std::atomic<bool> previewLoop { false }, previewOriginal { false }, previewRestart { false };
    void labPlay(bool on);                   // message thread: PLAY (from the start) / STOP
    void labPlaySection(double beatA, double beatB);   // PLAY SECTION: only that range of YOUR EDIT (loops with LOOP)
    std::atomic<int> previewRangeA { -1 }, previewRangeB { -1 };
    std::atomic<int> previewLoopA { -1 }, previewLoopB { -1 };     // a LOOP region inside the range (samples), -1 = loop the whole range
    std::atomic<bool> previewIsSection { false };                  // PLAY SECTION is playing (not PLAY)
    // the SAMPLE LAB strip's START / END / LOOP per recording (seconds; end < 0 = the end, loop a >= b = none). Saved with the project
    // (only when set: old projects have none and play exactly as before).
    struct StripMarks { double start = 0, end = -1, loopA = 0, loopB = 0; bool isDefault() const { return start <= 0 && end < 0 && loopB <= loopA; } };
    std::map<int, StripMarks> stripMarks;
    StripMarks marksOf(int id) const { auto it = stripMarks.find(id); return it == stripMarks.end() ? StripMarks() : it->second; }
    void labPlayOriginalRange(double aSec, double bSec);          // ORIGINAL + PLAY SECTION: just the strip's highlight of the recording
    bool stripToKeys(double aSec, double bSec, const juce::String& name = {});                    // the highlight (or START..END) -> the keys, with the LOOP region inside it
    void labRenderNow();                     // message thread: render the timeline right now (USE ON KEYS, tests)
    bool labUseOnKeys();                     // YOUR EDIT -> the sampler's sound (classic, START 0 / END 1), false if nothing to use
    bool useSoundOnKeys(std::shared_ptr<gs::SampleData> s);   // any sound -> the sampler's sound (the same settings)
    int labEditToRecording();                // YOUR EDIT -> a new Lab recording (made current), e.g. to polish it in PROCESS LAB; 0 if empty
    std::atomic<const gs::SampleData*> originalPtr { nullptr };
    std::atomic<int> originalStart { -1 }, originalEnd { -1 }, originalLoopA { -1 }, originalLoopB { -1 }; int originalRange[4] { -1, -1, -1, -1 };
    std::function<void()> onLabRendered;     // UI refresh hook (called on the message thread)
    double previewPosUi() const { return previewPos; }

    // ---- VOCAL CHOP (message thread)
    gs::ChopState chop;
    void chopLoad(std::shared_ptr<gs::SampleData> s, bool undoable = true);   // new audio + AUTO CHOP
    void chopBegin();                       // snapshot for UNDO: call before every edit
    void chopPushUndo(const gs::ChopState& before);   // ... or hand over the state from before a drag
    void chopChanged();                     // re-render the changed slices in the background, then swap them in
    void chopRenderNow();                   // the same, right now (tests, bounce)
    bool chopUndo(); bool chopRedo();
    bool chopCanUndo() const { return !chopUndoStack.empty(); }
    bool chopCanRedo() const { return !chopRedoStack.empty(); }
    void chopBounce();                      // print the slice set into a new sample, keep the lines
    void chopPublishPattern();              // CHOP MELODY steps -> audio thread
    std::atomic<bool> chopPlay { false };   // CHOP MELODY playing
    int chopStepUi() const { return chopStepNow.load(); }
    std::shared_ptr<gs::ChopKit> chopKitUi; // the kit playing now
    std::function<void()> onChopRendered;   // UI refresh hook (message thread)
    double hostBpm() const { return bpm; }

    juce::AudioProcessorValueTreeState apvts;
    gs::Engine engine;
    juce::MidiKeyboardState keyboard;   // the on-screen keyboard plays here too
    int currentPreset = 0;
    juce::String patchName { "Init" };
    uint32_t summonSeed = 1;

    // QUALITY (live) / RENDER QUALITY (offline bounce): the oversampling the voices and drive run at
    int wantedOversampling(bool offline) const;
    void applyQuality();                     // message thread: re-prepares the engine (under the callback lock) when QUALITY changed
    // VINTAGE character: the Sample Ghost as loaded (saved in the project) and the copy with its character (what plays)
    std::shared_ptr<gs::SampleData> rawSample;
    std::shared_ptr<gs::SampleData> withCharacter(std::shared_ptr<gs::SampleData> s) const;
    // the same character on every sample Poltergeist plays: CHOP slices and Sample Lab keys too (one shared control)
    std::shared_ptr<gs::ChopKit> rawChopKit;                  // the kit as rendered (the playing one is chopKitUi)
    std::shared_ptr<gs::ChopKit> withCharacter(std::shared_ptr<gs::ChopKit> k) const;
    std::shared_ptr<gs::SampleData> keyPlay[128];             // what each Lab key plays (keySamples[] = as mapped / saved)
    std::shared_ptr<gs::SampleData> keySample(int note) const { return juce::isPositiveAndBelow(note, 128) ? keySamples[note] : nullptr; }
    void applyCharacter();                   // message thread: re-renders the character copy when its settings changed

private:
    void timerCallback() override { applyQuality(); applyCharacter(); morphTimer(); }
    GhostSound soundFrom(const juce::String& source);
    float vcApplied[3] { 0, -1, -1 };
    double preparedRate = 0; int preparedBlock = 0; bool bypassWasSilent = false;
#if GHOSTY_PROCESS_LAB
    juce::ValueTree plabTree; juce::CriticalSection plabLock;
#endif
    gs::ParamReader reader;
    std::atomic<float>* morphOnRaw = nullptr; std::atomic<float>* morphXRaw = nullptr; std::atomic<float>* morphYRaw = nullptr;
    gs::Snap snap {};
    gs::MidiFx midiFx;
    std::vector<std::shared_ptr<gs::SampleData>> keepAlive;      // everything ever given to the audio thread
    std::vector<std::unique_ptr<gs::KeyMap>> keymaps;             // old maps stay alive for the session
    std::shared_ptr<gs::SampleData> keySamples[128];
    void publishKeys();
    std::atomic<const gs::SampleData*> previewPtr { nullptr }; double previewPos = 0;
    std::atomic<int> pitchGen { 0 }, renderGen { 0 };
    std::shared_ptr<std::atomic<bool>> pitchCancel;
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>>(true);
    double bpm = 120, ppq = 0; bool playing = false;
    // chop
    std::vector<gs::ChopState> chopUndoStack, chopRedoStack;
    std::vector<std::vector<gs::LabClip>> labUndoStack, labRedoStack;
    gs::ChopState::Cache chopCache;
    std::vector<std::shared_ptr<gs::ChopKit>> chopKits;   // every kit ever published stays alive (audio thread safety)
    std::atomic<int> chopGen { 0 };
    std::atomic<uint32_t> chopSteps[16] {}; std::atomic<int> chopNumSteps { 16 }, chopStepNow { -1 };
    int chopLastNote = -1; long chopLastStep = -1; double chopClock = 0; bool chopWasPlaying = false;
    juce::MidiBuffer merged;
    void publishChopKit(std::shared_ptr<gs::ChopKit> k);
    void chopSequence(juce::MidiBuffer& m, int n);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PoltergeistProcessor)
};
