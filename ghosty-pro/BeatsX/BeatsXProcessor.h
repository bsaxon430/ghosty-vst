#pragma once
#include "KeyNames.h"
#include "License.h"
#include "TempoMul.h"
#include "ExportGate.h"
#include "Rhythm.h"
#include "Destruction.h"
#include "BeatEngineHooks.h"   // BeatEngine.h + Linear.h + MelodyTracker.h, hooks bound
#include "Bypass.h"
#include "SyncTransport.h"
#include "AudioToMidi.h"
#include "GhostThemes.h"
#include "TrackerFiles.h"
#include "UserBanks.h"
#include "GhostKitBanks.h"   // BeatsX/banks: Block Step + Ghost House drum banks (juce_add_binary_data)
#include <map>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <deque>
#include <set>

class BeatsXProcessor : public juce::AudioProcessor, private juce::AsyncUpdater {
public:
#ifdef JucePlugin_VersionString
    static constexpr const char* kVersion = JucePlugin_VersionString;   // from CMake (juce_add_plugin VERSION)
#else
    static constexpr const char* kVersion = "3.0.0";                    // (test builds have no plugin defines)
#endif
    BeatsXProcessor();
    ~BeatsXProcessor() override;
    const juce::String getName() const override { return "Ghosty Beats"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    // main stereo out + 16 optional stereo track outs (MULTI-OUT: each track after its chain, before the bus)
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
        for (int i = 1; i < l.outputBuses.size(); ++i) if (!(l.outputBuses[i] == juce::AudioChannelSet::stereo() || l.outputBuses[i].isDisabled())) return false;
        return true;
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return (int) gb::allKits().size(); }
    int getCurrentProgram() override { return currentKit; }
    void setCurrentProgram(int i) override { if (juce::isPositiveAndBelow(i, getNumPrograms())) { applyKit(i, true); } }
    const juce::String getProgramName(int i) override { return juce::isPositiveAndBelow(i, getNumPrograms()) ? gb::allKits()[(size_t) i].name : ""; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void restoreState(const void* data, int size, bool keepBypass = false);   // file loads (no demo check); setStateInformation = host restore

    ghosty::License license { "Ghosty Beats" }; ghosty::DemoGate demo;   // paid version: license key + demo mode
    // DEMO EXPORT GATE (Shared/ExportGate.h, docs/DEMO-EXPORT-GATE.md): every user export (DRAG MIDI / AUDIO / STEMS, track drags,
    // DRAG MELODY MIDI, BEAT + MELODY, region / slice WAVs, XM / IT export) asks exportGate(ui, id) BEFORE it writes anything; the
    // processor functions that write the files re-check unlockedNow() silently (a back-stop). SAVE BEAT (.ghostbeat, the plugin's
    // own project format) is not an export and works in the demo.
    bool demoForTest = false;                                             // (tests: behave as a locked demo without touching the licence)
    bool unlockedNow() const { return ghosty::exportUnlocked(license) && !demoForTest; }
    bool exportGate(juce::Component* ui, const char* id) const { return ghosty::exportAllowed(license, ui, id, demoForTest); }
    // BYPASS (Shared/Bypass.h): mutes the whole plugin; a normal automatable parameter ("bypass", last in the list), never part of a kit / preset / undo
    juce::AudioParameterBool* bypassParam = nullptr; ghosty::BypassFader bypass;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }
    // PLAY: ALWAYS | ON NOTES (ROADMAP 16, FL channel-mute follow). ALWAYS (0, default) = the beat runs whenever the host / PLAY runs.
    // ON NOTES (1) = the pattern / song / melody run (clock-synced to the host) only while at least one MIDI note is held (any key, any channel):
    // the first note-on starts it at that sample on the host grid, the last note-off fades it out (~6 ms) and cuts every tail. In ON NOTES the
    // MIDI notes only gate the beat (they do NOT play pads or record); the on-screen pads still play. BYPASS wins. Saved with the project
    // (state property "playmode"), never in kits / beats / undo; projects saved before it existed load ALWAYS.
    std::atomic<int> playMode { 0 };
    struct NoteGate {   // audio thread only, no allocation
        uint8_t held[16][128] {}; int total = 0;
        enum State { Open, Fading, Closed } state = Open; float gain = 1.0f, step = 1.0f / 288.0f;
        void prepare(double sr) { step = 1.0f / (float) juce::jmax(1.0, 0.006 * sr); }
        void releaseAll() { for (auto& c : held) for (auto& n : c) n = 0; total = 0; }
        // one MIDI message: true if the gate's "a note is held" changed
        bool feed(const juce::MidiMessage& m) {
            const bool was = total > 0; const int ch = juce::jlimit(1, 16, m.getChannel()) - 1;
            if (m.isNoteOn()) { auto& h = held[ch][m.getNoteNumber()]; if (h < 255) { ++h; ++total; } }
            else if (m.isNoteOff()) { auto& h = held[ch][m.getNoteNumber()]; if (h > 0) { --h; --total; } }
            else if (m.isAllNotesOff() || m.isAllSoundOff() || m.isResetAllControllers()) releaseAll();
            return was != (total > 0);
        }
    } gate;
    bool wasHostPlaying = false, gateFresh = true; double gateRate = 48000;
    // ---- message-thread API for the editor
    gb::Model model;
    gb::mel::Song melody;                             // MELODIC TRACKER song (saved as BEATS2/MELODY; undo / redo carry it with the model)
    std::shared_ptr<gb::DrumLibrary> lib;
    juce::AudioProcessorValueTreeState apvts;
    gb::BeatEngine engine;
    int currentKit = 0;
    void publish();                                   // model -> new snapshot for the audio thread
    void requestVariants(bool all);                   // per-hit destruction renders for the sounds that need them (worker thread, then publish)
    bool variantsMissing() const;
    void applyKit(int kitIndex, bool patternsToo);
    bool loadSampleToTrack(int track, const juce::File& f, juce::String& err);
    int loadSamples(int firstTrack, const juce::StringArray& files, juce::String& msg);   // several files -> consecutive tracks
    int firstFreeTrack() const;                       // a track with no sound / no steps (-1 = none)
    void setTrackSound(int track, const juce::String& name);
    // SHARED SOUNDS: Documents/Ghosty/Shared Sounds (Ghosty Spectrum's SEND TO BEATS writes there). New files are added to the
    // browser's SHARED folder; a shared sound put on a track is kept inside the project like a dropped sample.
    static juce::File sharedSoundsDir() { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Shared Sounds"); d.createDirectory(); return d; }
    int rescanShared();                               // returns how many new sounds were added
    // MY BANKS (owner 2026-10-30, Shared/UserBanks.h): the drum banks shared with Block Step and Ghost House (Music/GhostBlock Banks).
    // Each bank is a browser folder ("MY · 808 KIT"); its sounds are typed (Kick, Snare...) so roles and GENERATE work with them; on a
    // track a bank sound is copied into the project like a dropped sample. GENERATE SOUNDS (genBanks): when banks are chosen, every
    // GENERATE first gives each unlocked track a new sound of the same kind from them ("builtin" = Ghost Kit's own sounds; empty = keep).
    int rescanBanks(bool force = false);              // message thread; returns how many sounds / banks changed
    struct MyBank { juce::String id, name; int count = 0; };
    std::vector<MyBank> myBanks;                      // in library order
    std::map<juce::String, juce::String> myBankOf;    // sound name -> bank id ("" = removed from its bank)
    std::map<juce::String, int> myUid;                // sound name -> library id
    juce::StringArray genBanks;
    int kitFromBanks(const juce::StringArray& banks); // returns how many tracks got a new sound
    int pickFromBanks(const juce::StringArray& banks, bool closest);
    // SONG KEY (owner 2026-10-30, Shared/KeyNames.h): -1 = off. With a key, drums whose name carries a key (CHIMAERA's "mau5 kick 07 Em")
    // are picked in the song's key (same root; the exact minor / major first; keyless sounds stay allowed) and setSongKey swaps every keyed
    // drum on a track to the same bank's sound in the new key (the closest name). Saved per project.
    int songKeyRoot = -1; bool songKeyMinor = true;
    int setSongKey(int root, bool minor);              // returns how many tracks got their in-key sound
    juce::String songKeyLabel() const { return songKeyRoot < 0 ? juce::String("KEY: OFF") : juce::String("KEY: ") + ghosty::keys::noteName(songKeyRoot) + (songKeyMinor ? " MIN" : " MAJ"); }
    // SOUNDS FROM (owner 2026-10-30): "auto" (default: each style gets the bank that fits it, your packs first), "stock" (Ghost Kit's own
    // sounds) or one bank (USE THIS BANK on the SOUNDS page). Loading a style or a bank puts the closest match on every track; GENERATE
    // keeps picking from the same bank(s).
    juce::String kitSource = "auto", autoPick;
    bool sourceApplied = false, freshBeat = true;
    juce::StringArray styleBanks(int kit);
    int applySource();   // returns how many tracks got a new sound
    void rememberSource() {}   // (2026-11-07: no longer the default for new beats; a new Ghosty Beats always starts fresh, the project keeps its own)
    juce::String sourceLabel() const;
    // BLOCK STEP / GHOST HOUSE BANKS (owner 2026-10-30): both plugins' drum banks built into Ghost Kit (BeatsX/banks, compiled in).
    // Ids bs1 bs2 gh1 gh2, and syn = bank 3 of both (the same built-in synth drums). Decoded the first time they're needed.
    void loadBuiltinBanks();
    bool builtinBanksLoaded = false;
    std::map<juce::String, juce::String> kitBankOf, kitBankFile;   // sound name -> bank id / its file in BeatsX/banks
    bool isBankSound(const juce::String& name) const { return kitBankOf.count(name) > 0; }
    static juce::StringArray defaultGenBanks() { return { "bs1", "bs2", "bs3", "gh1", "gh2", "gh3" }; }
    static bool genHas(const juce::StringArray& g, const juce::String& bankId) { return bankId == "syn" ? (g.contains("bs3") || g.contains("gh3")) : g.contains(bankId); }
    bool soundBytes(const juce::String& name, juce::MemoryBlock& mb, juce::String& ext, juce::String& type) const;   // for COPY into your banks
    bool isMine(const juce::String& name) const { return myUid.count(name) > 0; }
    void drainRecorded();                             // pads recorded while playing -> model
    juce::String categoryOf(const juce::String& sound) const { if (auto s = lib->byName(sound)) return s->category; return "Sample"; }
    juce::File userDir() const;
    // whole beats (kit, your samples, all 16 patterns, the song, the mixer) in Documents/Ghosty/Beats
    juce::File beatsDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Beats"); d.createDirectory(); return d; }
    bool saveBeat(const juce::String& name) { juce::MemoryBlock mb; getStateInformation(mb); return beatsDir().getChildFile(juce::File::createLegalFileName(name) + ".ghostbeat").replaceWithData(mb.getData(), mb.getSize()); }   // NOT-EXPORT: .ghostbeat, the plugin's own project format
    // Brandon's own Trap default (2026-11-21): a saved beat on HIS Mac (Documents/Ghosty/Beats/Default Trap.ghostbeat) opens with a new
    // Ghosty Beats and comes up when the Trap kit is picked. It lives only on his Mac, never inside the plugin (its sounds are from his own
    // packs); without the file everything is as before. BYPASS is never taken from it. (The test runs switch it off: useUserDefault.)
    static inline bool useUserDefault = true;
    juce::File defaultTrapFile() const { return beatsDir().getChildFile("Default Trap.ghostbeat"); }
    bool loadDefaultTrap() { if (! useUserDefault) return false; const auto f = defaultTrapFile(); juce::MemoryBlock mb;
        if (! f.existsAsFile() || ! f.loadFileAsData(mb) || mb.getSize() == 0) return false; restoreState(mb.getData(), (int) mb.getSize(), true); return true; }
    bool saveDefaultTrap() { juce::MemoryBlock mb; getStateInformation(mb); return defaultTrapFile().replaceWithData(mb.getData(), mb.getSize()); }
    bool loadBeat(const juce::File& f) { juce::MemoryBlock mb; if (!f.loadFileAsData(mb) || mb.getSize() == 0) return false; pushUndo(); restoreState(mb.getData(), (int) mb.getSize(), true); return true; }   // (a beat never changes BYPASS)
    bool savePattern(int pattern, const juce::String& name);
    bool loadPattern(const juce::File& f, int pattern);
    int songBars() const;
    int currentPattern() const { return juce::jlimit(0, gb::kPatterns - 1, (int) p[12]->load() - 1); }
    bool songModeOn() const { return p[11]->load() > 0.5f; }

    // ---- drag-out: MIDI and audio (24-bit WAV through the full engine, host tempo)
    int midiChannel = 1;                               // 1 (default) or 10, remembered in the settings
    juce::File exportMidi(bool song, int track = -1);  // GM-style drum notes; track >= 0 = only that track
    int midiNoteFor(int track) const;                   // the note a track exports as
    juce::File exportMelodyMidi(bool song);             // DRAG MELODY MIDI (gb::mel::toMidi)
    juce::File exportBeatAndMelodyMidi(bool song);      // DRAG BEAT + MELODY MIDI: one multi-track file
    double melodyExportBeats(bool song) const;          // one melody loop (its pattern / one pass of its OWN ORDER / the song)
    void previewMelodyNote(int inst, int note, float vel = 0.85f);   // audition at that pitch through the instrument's route track (never recorded); note < 0 = release
    bool melodyRowAt(double beat, int& pat, int& row) const;   // the melody row playing at that beat (nearest row)
    int drainMelodyMidi(const std::function<void(int note, int vel, double beat, bool on)>& f);   // MIDI keys played while the tracker's MIDI REC is armed
    // LINEAR preview: two bars of the resolved performance around the playhead: the hits as written + what LINEAR plays / dropped
    struct LinearView { double from = 0, beats = 8, span = 4; bool periodic = true, song = false; std::vector<gb::linear::Ev> orig, lin; gb::linear::Stats st; };
    LinearView linearView(double playBeat);
    gb::linear::PerfPtr linearPerfFor(const gb::Snapshot& S, const gb::EngineParams& P) const;   // the snapshot's Perf if it is current, else a fresh build
    void linearPoll();                                  // (message thread, 20 Hz) rebuild the Perf on a worker when a macro / swing / tempo change alters the hits
    uint64_t contentSerial = 0; gb::linear::PerfPtr previewPerf;
    juce::File bounce(bool song, int bars);            // render to WAV (for drag-out)
    juce::StringArray bounceStems(bool song, int bars); // one WAV per track that plays (kick, snare, hats...)
    juce::File bounceTrack(bool song, int bars, int track);
    int exportBars(bool song) const;
    // background audio render with progress + cache (same state -> same files, no re-render)
    struct AudioJob { bool song = false, stems = false; int track = -1; };
    void startAudioRender(AudioJob job);               // returns at once; renderProgress goes 0..1
    juce::StringArray waitAudioRender(AudioJob job);   // the files (renders now if not cached yet)
    std::atomic<float> renderProgress { 1.0f };
    juce::String audioKey(const AudioJob& j);          // a hash of what the render depends on (no serialising)
    juce::int64 modelHash() const;
    double renderRate = 48000;                         // drag-outs render at the host rate once it is known
    juce::StringArray renderNow(AudioJob job);        // render on this thread
    struct RenderSpec { AudioJob job; bool song = false, allowed = false; int bars = 1, pattern = 0, tempoMul = 1; gb::EngineParams P; std::shared_ptr<gb::Snapshot> snap; std::vector<gb::SoundPtr> keep; juce::StringArray names; };
    RenderSpec makeSpec(AudioJob j);
    juce::StringArray renderSpec(const RenderSpec& r, std::atomic<float>* progress, juce::Thread* onThread = nullptr);   // onThread: polled for threadShouldExit()

    // ---- rhythm tools (all undoable, all seeded; they edit the same steps the grid and tracker show)
    std::array<uint8_t, gb::kTracks> roles() const;
    std::array<bool, gb::kTracks> lockedTracks() const { std::array<bool, gb::kTracks> l {}; for (int t = 0; t < gb::kTracks; ++t) l[(size_t) t] = model.tracks[(size_t) t].lock; return l; }
    int kitStyle() const;                              // the GENERATE style of the current kit: DRILL / LATIN / TRAP CLASSIC keep their generators, every other kit its StyleGen.h profile
    void generateForKit();                             // GENERATE <STYLE>: generatePattern(kitStyle())
    bool varyKeepSounds = false;                       // VARY: the kit's sounds stay (GENERATE SOUNDS is skipped)
    void varyPattern();                                // VARY: the same family and kick foundation as the current pattern, new details (StyleGen.h)
    void generatePattern(int style);                   // 0 INTRICATE, 1 MACHINE GUN, 2 DRILL (also sets 138-146 BPM and a light 808 duck under the kick; DRILL GRIT), 3 REGGAETON (88-100 BPM), 4 TRAP CLASSIC (140-160 BPM)
    void keepIdentity(float keep);                     // KEEP x% IDENTITY, NEW HITS (current pattern)
    void blendPatterns(int a, int b, float w);         // BLEND two patterns into the current one
    bool chopTrack(int t, float sensitivity, bool writeRhythm);   // BREAK CHOP the track's sound
    // RE-CREATE AS MIDI (AudioToMidi.h): an analysed loop -> the current pattern (DRUMS) or its melody (PITCHED). Message thread;
    // the caller wraps it in ONE undo step. Returns a one-line summary for the status bar.
    juce::File exportSoundPart(int track, float a, float b, const juce::String& what);
    juce::File exportSound(const juce::String& name);   // ⇣ in the SOUNDS list: the whole sound as a 24-bit WAV named after it (the UI asks the gate)
    // MELODIC TRACKER <-> XM / IT files (TrackerFiles.h). Import (always allowed) replaces the melody song (the caller makes it one undo step);
    // export: the UI asks exportGate(); these re-check unlockedNow() (the demo gate back-stop).
    juce::String importTracker(const juce::File& f, bool& ok);
    bool exportTracker(const juce::File& dest, bool it, juce::String& msg);   // fractions a..b of the track's sound -> WAV (demo gate: none)
    juce::String applyRecreate(const gb::a2m::Loop& loop, const gb::a2m::Result& r, const gb::a2m::WriteOpts& w);
    void rearrangeTrack(int t);                        // new slice order keeping the groove
    std::vector<uint8_t> sliceRolesOf(int t) const;
    // BREED: 16 variations as tiles; star favourites; the next generation leans toward what they share
    std::vector<gb::Pattern> breedPool; std::vector<bool> breedStar; int breedGeneration = 0;
    void breedNext(int count = 16);
    void useBreed(int i);                              // copy a tile into the current pattern (undoable)
    int evolveBars = 16;                               // PERFORMANCE length for drag-out when EVOLVE is on (8 / 16 / 32 / 64)
    int bakePerformance(int bars);
    static float p_default(int i);
    // PHRASE LOCKS + MOTIFS (model.locks / model.motifs): generators, BREED and DNA keep these steps; EVOLVE plays them as written
    bool protectedStep(const gb::Pattern& p, int t, int s) const;
    void markProtected(gb::Pattern& p) const;     // temp-locks protected steps (flag bit 3)
    void unmarkProtected(gb::Pattern& p) const;   // removes the temp locks and re-imposes the motifs
    void applyMotifs(gb::Pattern& p) const;
    bool captureMotif(int track, int start, int len);   // from the current pattern's first bar (1/16 grid)
    std::shared_ptr<const gb::script::Program> compiledScript(const juce::String& code);
    std::map<juce::String, std::shared_ptr<const gb::script::Program>> scriptCache;
    struct Variants { gb::SoundPtr src; std::array<gb::SoundPtr, 8> v; };                    // src keeps the source alive, so its address can't be reused by another sound
    std::map<const gb::Sound*, Variants> variantCache;                                           // per-hit destruction renders, one set per sound (filled by the worker)

    // ---- undo / redo (every edit: steps, tracks, kits, samples, patterns)
    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    int undoDepth() const { return (int) undoStack.size(); }   // (tests)

    // ---- settings (favourites, normalise dropped samples, MIDI channel)
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::StringArray favourites;
    juce::int64 myStamp = -1;   // MY BANKS: the library.json we last read
    bool normaliseSamples = true;
    // editor look: the house themes BLACK / PURPLE / LIGHT (Shared/GhostThemes.h), remembered on this computer as "theme_name" (dark | purple | light);
    // the old "dark" switch still loads (true -> BLACK, false -> LIGHT); never saved before -> BLACK. FOCUS (grid only) is remembered too.
    ghosty::themes::Theme theme = ghosty::themes::Theme::Dark; bool focusMode = false;
    // GHOST (Beats-only 4th look: liquid glass on the LIGHT base): saved as theme_name "ghost" (theme = LIGHT meanwhile; older builds read
    // the unknown name as BLACK, as any unknown look always was). look() = 0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST.
    bool ghostLook = false;
    // NEON (Beats-only 5th look, owner 2026-11-11: faded 1995 hand-painted cyberpunk, rainy neon city): saved as theme_name "neon" (theme = BLACK
    // meanwhile, so older builds and the shared widgets read it as BLACK). look() = 4.
    bool neonLook = false;
    int look() const { return neonLook ? 4 : ghostLook ? 3 : ghosty::themes::toInt(theme); }
    // Brandon 2026-11-13: only NEON (4) and LIGHT (2, ink + watercolour) are left; any other look (an old saved one) becomes NEON
    void setLook(int l) { ghostLook = false; neonLook = l != 2; theme = neonLook ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light; }
    static bool ghostFromSettings(juce::PropertiesFile& s) { return s.getValue("theme_name").trim().equalsIgnoreCase("ghost"); }
    static bool neonFromSettings(juce::PropertiesFile& s) { return s.getValue("theme_name").trim().equalsIgnoreCase("neon"); }
    static ghosty::themes::Theme themeFromSettings(juce::PropertiesFile& s) {
        if (s.containsKey("theme_name")) return ghosty::themes::fromString(s.getValue("theme_name"));
        if (s.containsKey("dark")) return s.getBoolValue("dark") ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light;
        return ghosty::themes::Theme::Dark;
    }
    void saveSettings();
    void previewSound(const juce::String& name) { if (auto s = lib->loaded(name)) { keepSounds.push_back(s); engine.previewReq.store(s.get()); } }

    std::atomic<int> stutter { 0 }; std::atomic<bool> tape { false };
    std::atomic<bool> recording { false };
    std::atomic<double> hostBpm { 0.0 };   // last tempo the host reported (0 = none yet, e.g. standalone)
    // SYNC TO DAW (Shared/SyncTransport.h): DAW (default) or OWN clock + tempo + PLAY. Saved per project ("syncmode", "ownbpm"), never in kits / beats / undo.
    ghosty::SyncTransport sync;
    std::atomic<bool> hostPresent { false };   // the host gave a position last block (false = standalone / no transport)
    bool ownClock() const { return sync.getMode() == ghosty::SyncTransport::Mode::Own; }
    // TEMPO ½x | 1x | 2x (Shared/TempoMul.h): the beat runs at the DAW's tempo times this, locked to the DAW's bars. Saved with the project; 1x = fresh.
    std::atomic<int> tempoMul { ghosty::tempomul::Normal };
    double tempoFactor() const { return ghosty::tempomul::factor(tempoMul.load(std::memory_order_relaxed)); }
    juce::String midiTempoTag() const { return tempoMul.load() == ghosty::tempomul::Normal ? juce::String() : ghosty::tempomul::bpmText(effectiveBpm()) + "bpm" + ghosty::tempomul::fileTag(tempoMul.load()) + " "; }   // MIDI file names: "160bpm 2x "
    double effectiveBpm() const { return readParams(liveBpm()).bpm; }   // what you hear (the DAW's / own tempo x TEMPO)
    double liveBpm() const { return ownClock() ? sync.getOwnBpm() : hostBpm.load(); }   // the tempo renders / previews use (OWN: the own tempo)
    static constexpr float kAuditionVel = 0.9f;   // a click on the sound's waveform (and its WAV drag: the same hit)
    void auditionTrack(int t, float vel = kAuditionVel) { engine.pushPad(t, vel); }
    juce::AudioBuffer<float> renderTrackHit(int t, float a, float b, double sampleRate, float vel = kAuditionVel) const;   // one hit as heard (pre-bus), for the WAV drag
    gb::EngineParams readParams(double bpm) const;

    static int paramCountGlobal() { return 29; }
private:
    static BusesProperties makeBuses();
    void handleAsyncUpdate() override;                 // the variant worker finished a sound: into the cache + publish
    // the CLEAN limiter's 1.5 ms look-ahead is reported to the host as latency (0 with SOFT / OFF). The audio thread only stores the
    // wanted value; a message-thread timer reports it (nothing is posted from the audio thread).
    struct LatencyPoll : juce::Timer { BeatsXProcessor& p; int reported = 0; explicit LatencyPoll(BeatsXProcessor& o) : p(o) { startTimer(100); } void timerCallback() override { const int w = p.wantLatency.load(); if (w != reported) { reported = w; p.setLatencySamples(w); } } } latencyPoll { *this };
public:
    std::atomic<int> wantLatency { 0 };
private:
    juce::MidiBuffer noMidi;   // (always empty: in ON NOTES the notes gate the beat and never reach the pads)
    void renderGated(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const gb::EngineParams& P, double bpm, double ppq, bool playing, float* const* aux);
    void renderSegment(juce::AudioBuffer<float>& buffer, int from, int to, const gb::EngineParams& P, double bpm, double ppq, bool playing, float* const* aux);
    struct VariantWorker; std::unique_ptr<VariantWorker> worker;
    struct LinearWorker; std::unique_ptr<LinearWorker> linWorker;
    std::deque<std::pair<gb::linear::PerfPtr, double>> keepPerfs;   // every Perf the audio thread may still read (it finishes a bar on the old one)
    void keepPerf(const gb::linear::PerfPtr& p);
    struct LinearPollTimer : juce::Timer { BeatsXProcessor& p; explicit LinearPollTimer(BeatsXProcessor& o) : p(o) { startTimer(50); } void timerCallback() override { p.linearPoll(); } } linearPollTimer { *this };
    std::set<const gb::Sound*> variantsRequested;
    juce::File renderTo(const gb::Snapshot* s, const gb::EngineParams& P, int bars, const juce::File& f, juce::Thread* onThread);
    std::vector<std::unique_ptr<gb::Snapshot>> snaps;   // older snapshots stay alive (audio may still read the last one)
    std::vector<gb::SoundPtr> keepSounds;
    struct MipsEntry { gb::SoundPtr src; std::shared_ptr<const gb::mel::Mips> mips; };
    std::map<const gb::Sound*, MipsEntry> melMips; std::vector<std::shared_ptr<const gb::mel::Mips>> keepMips;
    std::map<const void*, double> lastSeen; double lastTrim = 0;
public:
    double keepMs = 30000.0;   // how long a sound no snapshot uses is kept (tests shorten it)
    void trimKeeps(bool force = false);
    size_t keptSounds() const { return keepSounds.size(); }
private:   // keepSounds / keepMips: freed 30 s after the last snapshot that used them   // melody instruments' mip levels (made at publish)
    std::atomic<float>* p[32] {};
    std::atomic<float>* tp[gb::kTracks][7] {};         // per-track automation: vol pan pitch decay tone duck character
    struct UndoState { gb::Model m; gb::mel::Song s; };
    std::deque<UndoState> undoStack, redoStack;
    struct RenderThread; std::unique_ptr<RenderThread> renderThread; std::vector<std::unique_ptr<RenderThread>> retiring;   // old renders told to stop, reaped when they are gone
    juce::CriticalSection renderLock; juce::String cachedKey; juce::StringArray cachedFiles;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatsXProcessor)
};
