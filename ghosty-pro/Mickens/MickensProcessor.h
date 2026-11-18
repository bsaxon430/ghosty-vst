#pragma once
#include "ExportGate.h"   // License + the demo export gate
#include "MickensEngine.h"
#include "BreakDNA.h"
#include "BreakSearch.h"
#include "MkPresets.h"
#include "Bypass.h"
#include "SyncTransport.h"
#include <map>
#include <atomic>
#include <bitset>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <future>

class MickensProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    MickensProcessor();
    ~MickensProcessor() override;
    const juce::String getName() const override { return "Mr. Mickens"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Mr. Mickens"; }
    void changeProgramName(int, const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void restoreState(const void* data, int size, bool fromHost = true);   // no demo check; setStateInformation = host restore; fromHost false = preset / .mickens file load (keeps PLAY mode)

    ghosty::License license { "Mr Mickens" }; ghosty::DemoGate demo;   // paid version: license key + demo mode
    // BYPASS (Shared/Bypass.h): the last parameter; mutes the whole plugin, never part of presets / slots / morph / sessions
    juce::AudioParameterBool* bypassParam = nullptr; ghosty::BypassFader bypass; bool bypassStopPending = false;
    // PLAY: ALWAYS | ON NOTES (ROADMAP 16, FL channel-mute follow): in ON NOTES the beat runs only while a MIDI note is held (any key;
    // the notes keep their own jobs). Saved per session, never in presets / slots / morph / DICE; old sessions load ALWAYS.
    std::atomic<float>* playMode = nullptr; ghosty::BypassFader gate; bool gateStopPending = false, lastHostPlay = false; std::bitset<128> heldNotes; int heldCount = 0;
    std::atomic<bool> gateOpenUi { true };
    // SYNC TO DAW | OWN (Shared/SyncTransport.h). Mode + own tempo are saved per project, never in presets / slots / morph / DICE / undo.
    ghosty::SyncTransport sync; std::atomic<bool> ownClockUi { false }; bool ownClockWas = false;
    bool ownTransport() const { return sync.getMode() == ghosty::SyncTransport::Mode::Own || ownClockUi.load() || wrapperType == wrapperType_Standalone; }   // the UI's PLAY / SPACE drive the own clock (OWN, or no host transport e.g. the standalone app)
    // ---- message-thread model
    juce::AudioProcessorValueTreeState apvts;
    mk::Engine engine;
    // the break list, in menu order: your Breaks folder (real records you own), the 100 built-in re-creations,
    // then breaks dropped onto a bay (those are saved inside the session). Bays remember breaks by key, not index.
    std::vector<mk::BreakPtr> folderBreaks;
    std::shared_ptr<const mk::BreakLibrary> lib;   // built once per app in the background (DNA included), shared by every instance
    std::vector<mk::BreakPtr> userBreaks;
    std::atomic<int> listVersion { 0 };           // bumps whenever the list changes (editor rebuilds its menus)
    std::array<juce::String, mk::kBays> bayKey { "Haunt|Haunt - Original", "Static|Static - Original", "Two Step|Two Step - Ghosts", "Canyon|Canyon - Original" };
    static juce::String keyOf(const mk::Break& b) { return b.family + "|" + b.name; }
    juce::File breaksFolder() const { auto d = saveDir().getChildFile("Breaks"); d.createDirectory(); return d; }
    void rescanFolder();                          // background scan of the Breaks folder (shared by every instance, never waited for)
    bool libReady() const { return lib != nullptr; }
    int keyIndex(const juce::String& key) const { for (int i = 0; i < numBreaks(); ++i) if (keyOf(*breakAt(i)) == key) return i; return -1; }
    void pickBreak(int bay, int index) { if (auto br = breakAt(index)) { bayKey[(size_t) bay] = keyOf(*br); bayBreak[(size_t) bay] = index; publish(); } }
    gb::DrumLibrary drums;
    std::array<int, mk::kBays> bayBreak { -1, -1, -1, -1 };   // index into the list (from bayKey; -1 = not loaded yet)
    std::array<std::array<uint8_t, mk::kSteps>, mk::kDrums> pattern {};
    std::array<juce::String, mk::kDrums> drumName { "Hard Kick", "Big Snare", "Tight Hat", "Short Open Hat" };
    std::array<gb::SoundPtr, mk::kDrums> userDrum {};   // a sample dropped on a drum lane

    int numBuiltIn() const { return lib ? (int) lib->breaks().size() : 0; }
    int numBreaks() const { return (int) folderBreaks.size() + numBuiltIn() + (int) userBreaks.size(); }
    mk::BreakPtr breakAt(int i) const {
        if (i < 0) return nullptr;
        if (i < (int) folderBreaks.size()) return folderBreaks[(size_t) i];
        i -= (int) folderBreaks.size();
        if (i < numBuiltIn()) return lib->breaks()[(size_t) i];
        i -= numBuiltIn();
        return i < (int) userBreaks.size() ? userBreaks[(size_t) i] : nullptr; }
    static juce::String upgradeKey(const juce::String& k) {   // old family names (hashed table) -> the current ones
        const auto fam = k.upToFirstOccurrenceOf("|", false, false), nf = mk::renamedFamily(fam);
        return nf == fam ? k : nf + "|" + k.fromFirstOccurrenceOf("|", false, false).replace(fam + " - ", nf + " - "); }
    void resolveBays() { for (int b = 0; b < mk::kBays; ++b) { bayKey[(size_t) b] = upgradeKey(bayKey[(size_t) b]); bayBreak[(size_t) b] = keyIndex(bayKey[(size_t) b]); } ++listVersion; publish(); }
    gb::SoundPtr drumSound(int d) const { return userDrum[(size_t) d] ? userDrum[(size_t) d] : drums.byName(drumName[(size_t) d]); }
    static juce::StringArray drumChoices(int d);

    void publish();
    void setPlaying(bool on) { if (on && !engine.playing.load()) engine.restart(); engine.playing.store(on); }
    bool addUserBreak(const juce::File& f, int bay, juce::String& err);
    bool setUserDrum(int d, const juce::File& f, juce::String& err);
    void defaultPattern(int style);   // 0 two-step, 1 halftime, 2 roller, 3 empty
    void diceStarts();                // the DICE button: new start for every bay (on its grid)
    // one parameter write with a host gesture around it; unchanged values are skipped (bulk recalls don't flood automation)
    void setParam(const juce::String& id, float value);

    juce::File saveDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Mr Mickens"); d.createDirectory(); return d; }
    // DEMO GATE (Shared/ExportGate.h): every export (drag-out WAV / stems / bay MIDI, the LOOP SECTION's MIDI / WAV grips) is part of
    // the full version. exportAllowed(ui, id) = the gate, asked on the message thread before any render (refused: the UNLOCK hint);
    // exportOk() = the silent back-stop the renders ask on the worker thread. Saved setups / presets (.mickens: parameters + the
    // breaks / drum samples YOU dropped in) are not exports and work in demo. Tests may swap exportGate (false = locked).
    std::function<bool()> exportGate;
    bool exportAllowed(juce::Component* ui, const char* id) { return ghosty::exportAllowed(license, ui, id, exportGate && !exportGate()); }
    bool exportOk() const { return (exportGate ? exportGate() : true) && ghosty::exportUnlocked(license); }
    bool writeSetup(const juce::File& f) { juce::MemoryBlock mb; getStateInformation(mb); return f.replaceWithData(mb.getData(), mb.getSize()); }   // NOT-EXPORT: a .mickens setup (project state)
    bool saveSession(const juce::String& name) { return writeSetup(saveDir().getChildFile(juce::File::createLegalFileName(name) + ".mickens")); }
    bool loadSession(const juce::File& f) { juce::MemoryBlock mb; if (!f.loadFileAsData(mb) || mb.getSize() == 0) return false; restoreState(mb.getData(), (int) mb.getSize(), false); return true; }
    juce::String describeMix() const;            // "Bay 1+2 Kick Hat" for file names
    juce::File bounce(int bars);                 // whole mix to WAV (drag out); safe to call from a worker thread
    juce::StringArray bounceStems(int bars);     // breaks / kick / snare / hats
    double currentBpm() const { return lastBpm.load(); }
    std::atomic<double> lastBpm { 172 };
    std::atomic<bool> hostRunning { false };
    std::atomic<int> renderBusy { 0 };           // drag-out renders in flight (snapshots are not freed meanwhile)
    int theme = 0;   // UI look, saved with the session: 0 PURPLE (default), 1 BLACK, 2 LIGHT, 3 GHOST (liquid glass on LIGHT; older builds read 3 as LIGHT)
    bool stutterLatch = false; // the LATCH switch (UI only; the stutter parameter itself is what's automated)

    // ---------------- Mickens 2
    std::atomic<bool> legacy { false };   // a session saved before Mickens 2: the classic engine paths keep sounding exactly as they did
    // BREAK DNA (built-ins and folder breaks carry theirs; dropped files are analysed once on the drop)
    std::shared_ptr<const mk::BreakDNA> dnaOf(const mk::BreakPtr& br);
    std::shared_ptr<const mk::BreakDNA> bayDNA(int bay) { return dnaOf(breakAt(bayBreak[(size_t) bay])); }
    std::array<std::shared_ptr<const mk::BreakDNA>, mk::kBays> frozen {};   // FREEZE DNA
    void syncFreeze();                     // follows the freeze switches (also when automated)
    // DNA undo: every DNA move (mode, remix, lock, dice, freeze) can be taken back
    void dnaPush(int bay);
    bool dnaUndo(int bay);
    void dnaDice(int bay);
    std::vector<std::vector<float>> dnaHistory[mk::kBays];
    static juce::StringArray dnaParamIds(int bay);
    // STEP LOCKS + the SEQUENCE strip: what each bay will play per 16th, lock single steps, bake, export as MIDI
    std::array<std::shared_ptr<const mk::StepLocks>, mk::kBays> locks {};
    mk::StepPlan previewPlan(int bay, long step);     // the plan with the current settings (message thread)
    void previewRow(int bay, mk::StepPlan* out, int steps, long from = 0);   // `steps` consecutive plans, one parameter read
    void lockStep(int bay, int step);                  // keep step (0..31) exactly as it plays now
    void unlockStep(int bay, int step);
    void clearLocks(int bay);
    void printLocks(int bay);                          // bake the current 2-bar plan into 32 locks
    bool stepLocked(int bay, int step) const { auto& L = locks[(size_t) bay]; return L && L->s[(size_t) juce::jlimit(0, 31, step)].on; }
    juce::File exportMidi(int bay, int bars);
    // LOOP SECTION drag-out: bars b0 .. b1-1 (0-based) only, rendered from the section's own bar; "Mr Mickens 140bpm bars 3-6"
    juce::File bounceRange(int b0, int b1);            // WAV (+ 2 s tail), worker thread
    juce::File exportMidiRange(int b0, int b1);        // MIDI: one track per unmuted bay + the drum machine
    juce::String rangeName(int b0, int b1) const;
    void bayMidi(int bay, long s0, long s1, juce::MidiMessageSequence& seq, int channel);
    std::atomic<int> secA { -1 }, secB { -1 };         // the section (bars, end exclusive; -1 = none), saved with the session
    std::atomic<bool> secLoop { false };               // LOOP SECTION: playback loops only those bars (default off, saved with the session)
    bool sectionLooping() const { const int a = secA.load(), b = secB.load(); return secLoop.load() && a >= 0 && b > a; }
    // OWN PLAY / SPACE / play-from-bar: where the own clock starts (the loop's timeline starts at 0 = the section start)
    double ownStartFor(int bar) const { if (!sectionLooping()) return juce::jmax(0, bar) * 4.0; const int a = secA.load(), b = secB.load(); return bar >= a && bar < b ? (bar - a) * 4.0 : 0.0; }          // one clip per bay: note 60 + slice (the KEYS mapping), ratchets as repeats
    // BREAK SEARCH / MATCH
    mk::SearchIndex& searchIndex();        // descriptors for every break in the list (built on first use; the DNA already exists)
    void audition(int index);              // play a break once, on its own, in time
    void stopAudition() { engine.audition.store(nullptr); engine.auditionStart.store(true); }
    // SNAPSHOT DECK: 8 slots that hold everything (every parameter + breaks, drum pattern, drum sounds, frozen DNA, locks)
    static constexpr int kSlots = 8;
    struct Slot { bool used = false; juce::ValueTree data; };
    std::array<Slot, kSlots> slots;
    void storeSlot(int i);
    void recallSlot(int i);
    void clearSlot(int i) { if (juce::isPositiveAndBelow(i, kSlots)) { slots[(size_t) i] = {}; refreshMorph(); } }
    juce::ValueTree slotTree(int i) const;  // what a slot holds (for tests: byte-identical round trips)
    void refreshMorph();                    // re-arm the morph after slots or A/B change
    bool morphing() const { return morphSet.load() != nullptr; }
    std::atomic<int> morphSideUi { 0 };
    // PRESETS: factory bank (MkPresets.h) + your own in Documents/Ghosty/Mr Mickens/Presets
    int currentPreset = -1; juce::String presetName;   // -1 = none / edited
    void loadFactoryPreset(int i);
    juce::File presetDir() const { auto d = saveDir().getChildFile("Presets"); d.createDirectory(); return d; }
    bool saveUserPreset(const juce::String& name) { presetName = name; currentPreset = -1; juce::MemoryBlock mb; getStateInformation(mb); return presetDir().getChildFile(juce::File::createLegalFileName(name) + ".mickens").replaceWithData(mb.getData(), mb.getSize()); }   // NOT-EXPORT: a .mickens preset
    juce::Array<juce::File> userPresets() const { auto f = presetDir().findChildFiles(juce::File::findFiles, false, "*.mickens"); f.sort(); return f; }
    bool loadUserPreset(const juce::File& f) { if (!loadSession(f)) return false; presetName = f.getFileNameWithoutExtension(); currentPreset = -1; return true; }

    // all parameters by index (morph / snapshots read them this way)
    std::vector<juce::RangedAudioParameter*> plist; std::vector<std::atomic<float>*> praw; std::vector<uint8_t> pkind;   // 0 continuous 1 stepped 2 not in snapshots
    void readParams(std::array<mk::BayParams, mk::kBays>& B, mk::FxParams& F, const float* vals) const;
    void liveValues(std::vector<float>& v, int side = -1) const;   // side: -1 = what the audio thread uses now
    size_t snapshotCount() const { return snaps.size() + keepMorph.size(); }   // tests: the garbage collector keeps this small
    const mk::Snapshot* currentSnapshot() const { const MorphSet* ms = morphSet.load(); return ms ? (morphAmt->load() >= 0.5f ? ms->snapB : ms->snapA) : liveSnap.load(); }

private:
    void timerCallback() override;
    std::shared_future<std::vector<mk::BreakPtr>> folderScan; bool folderPending = false;
    std::map<mk::BreakPtr, std::shared_ptr<const mk::BreakDNA>> dnaCache; std::vector<std::shared_ptr<const mk::BreakDNA>> keepDNA; std::vector<std::shared_ptr<const mk::StepLocks>> keepLocks;
    std::unique_ptr<mk::SearchIndex> search; int searchVersion = -1;
    // morph: two published models + the two slots' values, swapped in atomically
    struct MorphSet { std::vector<float> a, b; const mk::Snapshot* snapA = nullptr; const mk::Snapshot* snapB = nullptr; };
    std::atomic<const MorphSet*> morphSet { nullptr }; std::vector<std::unique_ptr<MorphSet>> keepMorph;
    std::atomic<int> morphSide { 0 }; const MorphSet* audioMs = nullptr; std::atomic<const MorphSet*> audioMsDone { nullptr }; std::atomic<const mk::Snapshot*> liveSnap { nullptr }; bool lastMorphOn = false; int lastMorphA = -1, lastMorphB = -1;
    std::vector<float> valsBuf;
    mk::Snapshot* buildSnapshot(const juce::ValueTree& model);
    juce::ValueTree modelTree() const;
    void applyModel(const juce::ValueTree& m);
    void gcSnapshots();   // frees snapshots / morph sets the audio thread has moved past
    int pIndex(const juce::String& id) const;
    std::vector<int> pi;   // indices used by readParams
    juce::File renderTo(uint32_t mask, int bars, const juce::File& f, int startBar = 0);
    std::vector<std::unique_ptr<mk::Snapshot>> snaps; uint64_t genCounter = 0;
    std::vector<mk::BreakPtr> keepBreaks; std::vector<gb::SoundPtr> keepSounds;
    struct BayP { std::atomic<float> *nudge, *grid, *pitch, *level, *decay, *pan, *rs, *rc, *ra, *dk, *ds, *dh, *mute, *fit; } bp[mk::kBays] {};
    std::atomic<float>* fx[48] {};
    std::atomic<float>* morphOn = nullptr; std::atomic<float>* morphAmt = nullptr; std::atomic<float>* morphA = nullptr; std::atomic<float>* morphB = nullptr;
    float muteGain = 1.0f; bool hostHasTempo = false; double curSr = 48000;   // the rate prepareToPlay got (a host sets getSampleRate() too; this never reads 0)
    juce::AudioBuffer<float> chunk; juce::MidiBuffer subMidi;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MickensProcessor)
};
