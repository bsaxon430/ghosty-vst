#pragma once
// Professor Bernstein's ArpEZator: chords -> arp -> melody idea machine. Instrument that also sends MIDI (lanes on
// channels 1 ARP, 2 MELODY, 3 CHORDS, 4 BASS), host-synced or free-running, with a small preview synth.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Idea.h"
#include "HookLab.h"
#include "ArpSynth.h"
#include "Listen.h"
#include "License.h"
#include "TempoMul.h"
#include "ExportGate.h"
#include "Bypass.h"
#include "GhostThemes.h"
#include "SyncTransport.h"
#include <array>
#include <atomic>
#include <functional>

void arpLoadEmbeddedRecipe();
constexpr int kLaneVols = 5;   // chords, arp, melody, bass, kick
inline const char* laneVolId (int k) { static const char* id[] { "lane_vol_chords", "lane_vol_arp", "lane_vol_melody", "lane_vol_bass", "lane_vol_kick" }; return id[k < 0 ? 0 : k > 4 ? 4 : k]; }
inline const char* laneVolName (int k) { static const char* n[] { "Chords", "Arp", "Melody", "Bass", "Kick" }; return n[k < 0 ? 0 : k > 4 ? 4 : k]; }
inline float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }   // fills arp::recipe() from the embedded data/recipe.json (the processor does it once)

// immutable playback data built on the message thread, handed to the audio thread lock-free
struct ArpPattern
{
    enum Type : uint8_t { LeadStart = 0, LeadLegato, LeadGlide, Chord, Bass };
    struct Ev { double tick = 0; uint8_t type = 0, lane = 0, pitch = 60, n = 0; float vel = 0.8f; double dur = 24; int chain = -1; std::array<uint8_t, arpsyn::kMaxPitches> ps {}; float energy = -1.0f; float fat = 0.0f; int idx = -1; };   // idx: the note's index in its lane (YOUR SAMPLE SLICES)   // energy: arp EVOLVE curve (-1 = none); fat: the FATTEN connector into this note (YOUR SAMPLE layers)
    struct MEv { double tick = 0; double dur = 24; uint8_t ch = 1, pitch = 60, vel = 100, lane = 0; uint8_t kind = 0; int val = 0; };   // kind 0 note, 1 pitch bend (val 0..16383), 2 CC (pitch = number)
    struct Span { int t = 0, tEnd = 0; int root = 0; uint16_t mask = 0; };
    int len = arp::BAR * 8; double bpm = 120;
    std::vector<Ev> ev; std::vector<MEv> midi; std::vector<Span> spans; int chordLoop = arp::BAR * 8;
    bool pianoTest = false;   // HOOK LAB PIANO TEST: plain piano, no FX, chords + the hook only
    int offset = 0;           // LOOP SECTION: this pattern is the idea from tick 'offset' (len = the section)
};

class ArpProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    ArpProcessor();
    ~ArpProcessor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ArpEZator"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }
    juce::AudioParameterBool* bypassParam = nullptr;   // BYPASS (last parameter): mutes the plugin, stops every note on the MIDI out
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- the idea (message thread; the lock also covers state save / load from other threads)
    juce::CriticalSection ideaLock;
    const arp::Idea& idea() const { return I; }
    int chordRewriteSeq = 0, chordRewriteKind = 0;   // EDITED CHORDS rebuilt by a change (1 replaced, 2 locked notes kept): the editor toasts it
    std::atomic<int> version { 0 };   // bumps on every change (editor repaints)
    // change the idea: pushes an undo point (labelled for the toast), rebuilds the playback pattern
    void change (const std::function<void (arp::Idea&)>& fn, bool undoPoint = true, const juce::String& label = "edit");
    void pushUndoPoint (const juce::String& label);   // before a change made elsewhere (sound knobs, lane volumes): one step
    juce::String undo();                              // returns what was undone ("" = nothing)
    juce::String redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    static constexpr int kUndoSteps = 60;
    // every mode keeps its own idea (+ sound): switching stores this one and brings the other back; only GENERATE replaces
    void switchMode (int mode);
    bool modeHasIdea (int mode) const { return mode == I.mode || modeStash[(size_t) juce::jlimit (0, arp::kMoods - 1, mode)].isNotEmpty(); }
    // the whole session (idea, the other modes' ideas, the sound) as one string: undo snapshots, tests
    juce::String sessionString() const;
    void restoreSession (const juce::String& s);
    // audition: a note through the preview synth (piano keys, clicked notes); message thread
    void audition (int pitch, bool on);
    // offline render through the same processBlock path (a private processor): laneMask bits 0..3 = lanes, bit 4 = kick; -1 = as heard
    struct RenderJob { arp::Idea idea; juce::String sound; double rate = 48000, bpm = 120, tail = 3.0; juce::MemoryBlock sampleFlac; arpsmp::Params sampleParams; int secStart = -1, secEnd = -1; std::array<float, 16> laneFx {}; };   // (+ YOUR SAMPLE, + DRAG SECTION)
    RenderJob renderJob (double rate, double bpm) const;   // snapshot (message thread)
    static juce::AudioBuffer<float> renderOffline (const RenderJob& job, int laneMask, std::atomic<float>* progress = nullptr);
    static bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate);   // 24-bit, never overwrites (caller picks the name)
    void generate (int mode, bool newSeed, bool undoPoint = true);
    void applyMoodSound (int mode);
    void applyFlavourLanes (bool force);
    void applyDarknessSound();             // TRAP / HIP HOP: DARKNESS -> the preview cutoff   // TRAP / HIP HOP: the lanes' own sounds for the idea's FLAVOUR (lanes the user changed are kept unless force)
    void generateAsync (int mode, bool newSeed);      // GENERATE on the worker (the editor's buttons): the window never waits
    void stepHistoryAsync (int dir);
    std::atomic<bool> ideaBusy { false }; int ideaJob = 0; bool asyncUi = false;   // asyncUi: the editor is open (mode switches generate on the worker)
    void runIdeaJob (const juce::String& label, std::function<void (arp::Idea&)> work, std::function<void()> after);
    std::unique_ptr<juce::Thread> ideaWorker;                   // the mood's preview sound (preset + LFO / FX defaults)   // GENERATE (replaces this mode's idea)
    void recallSeed (uint32_t seed);
    void stepHistory (int dir);                       // SEED < >
    void laneTool (int lane, const juce::String& tool, const std::vector<bool>* sel);
    void harvest (const std::vector<bool>& sel, int barFrom, int barTo);   // ARP -> MELODY
    void loadStarter (int index);
    juce::StringArray starterNames() const;
    // drag-out: writes a .mid (never overwrites) and returns it
    juce::File writeMidiFile (int laneMask, bool oneTrack);
    arp::MidiExport midiExport() const;   // glide bends / bend range / lane VOL as velocity, from the parameters
    double hostOrIdeaBpm() const { const double b = hostBpm.load(); return b > 1 ? b : (double) I.g.bpm; }
    // TEMPO ½x | 1x | 2x (Shared/TempoMul.h): ArpEZator runs at the DAW's (or the idea's / own) tempo times this, locked to the DAW's bars.
    std::atomic<int> tempoMul { ghosty::tempomul::Normal };
    double tempoFactor() const { return ghosty::tempomul::factor (tempoMul.load (std::memory_order_relaxed)); }
    double effectiveBpm() const { return hostOrIdeaBpm() * tempoFactor(); }   // what you hear
    std::vector<uint8_t> toHostTempo (const std::vector<uint8_t>& smf) const;   // a MIDI file written at the effective tempo -> the DAW's tempo, times scaled
    // user presets in Documents/Ghosty/ArpEZator/
    static juce::File presetDir();
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File& f);

    // ---- HOOK LAB (message thread). Auditions play a tile in place of the MELODY lane (same tempo, chords, sound)
    // without touching the idea; USE THIS / MAKE IT A SONG / RESPONSE are undo points. The lab (tiles, stars, its own
    // STYLE / CANDY / TENSION / REGISTER) is saved with the session and comes back with UNDO.
    struct HookLabState
    {
        std::vector<arp::Hook> tiles; int selected = -1, playing = -1, bars = 4; uint32_t seed = 1; double similarity = 0.6; std::vector<arp::Fragment> harvest; bool piano = false;
        int style = 0, candy = 0, reg = arp::RegMid; double tension = -1;   // the lab's own settings (the MELODY lane keeps its own)
        int meter = 0, euclidHits = 0, euclidRot = 0, feel = 0;           // IDM: METER / EUCLID / ROTATE, FEEL: NORMAL / HALF-TIME / DROP
        bool autoPlay = false;                                                 // AUTO-PLAY: the next tile after one pass
        bool chordsChanged = false;                                            // the chords changed under the tiles (refit / dropped)
        std::vector<std::vector<arp::Hook>> history; int histIdx = -1;         // earlier grids (< >)
        bool busy = false; int busyKind = 0;                                   // generating in the background: 1 GENERATE, 2 FIND THE MAGIC
    };
    HookLabState lab;
    arp::HarvestWeights taste; bool persistTaste = false;   // (2026-11-07: off. A new ArpEZator is always a fresh start; the harvester learns within this instance only, nothing read from or written to disk)   // the harvester's learned feature weights (Documents/Ghosty/ArpEZator/hooklab_taste.txt), loaded on first use
    void labGenerate (int bars);                     // GENERATE 16 HOOKS over the current chords (synchronous; the editor uses the Async one)
    void labGenerateAsync (int bars);                // the same on the lab worker (progress in labProgress, cancel with labCancel)
    void labMagic();                                 // FIND THE MAGIC: variations of the selected tile (starred tiles stay)
    void labMagicAsync();
    void labCancel();                                // stop a background GENERATE / FIND THE MAGIC
    std::atomic<int> labProgress { 0 };              // tiles made by the running job
    void labSetParams (int style, int candy, double tension, int reg);   // the lab's STYLE / CANDY / TENSION / REGISTER: new unstarred tiles
    void labSetRhythm (int meter, int euclidHits, int euclidRot, int feel);   // the lab's METER / EUCLID / ROTATE / FEEL: new unstarred tiles
    void labStepHistory (int dir);                   // < > through earlier grids
    void labAudition (int tile);                     // -1 = back to the MELODY lane
    void labPianoTest (bool on);                     // PIANO TEST: auditions play as plain piano (flat velocity, no glides, no FX, chords + hook)
    void labStar (int tile);                         // toggle; a star is a keep the harvester learns from
    void labUse (int tile);                          // USE THIS: into the MELODY lane (undo point)
    juce::String labSong (int tile, int bars);       // MAKE IT A SONG: 16 / 32 bars (undo point); returns what changed besides the MELODY lane
    int  labCombine (int rhythm, int opening, int ending);   // COMBINE -> the new tile's index (-1 = no tiles, -2 = every slot starred)
    juce::File writeHookMidi (int tile);             // a tile as a .mid (drag a tile out)
    void respond (double similarity);                // GENERATE RESPONSE: the phrase's second half answers its first
    void lockMotif (const std::vector<bool>& sel);   // LOCK MOTIF toggle (selected MELODY notes, or the strongest motif)
    void labHarvestRank();                           // MOTIF HARVESTER: rank fragments (lab.harvest)
    void labHarvestKeep (int k);                     // keep fragment k: it becomes the locked motif, the weights learn
    void ensureTaste();                              // load the learned weights (first lab use)
    void saveTaste();                                // queue a save (read-modify-write, debounced in the timer)
    void flushTaste();                               // write now (merges with other instances' keeps)
    bool auditioningHook() const { return melOverrideOn; }
    void userToggledPlay() { labStartedPlay = false; }   // PLAY / space: the transport is the user's now
    // PLAY FROM THE CURSOR (session only, not saved): the last click in a roll; SPACE starts the internal transport there
    std::atomic<int> playCursor { -1 };              // ticks; -1 = the beginning
    // LOOP SECTION (session / UI state, one brace per mode, never in presets / moods / undo): playback repeats [start, end)
    struct Section { int start = 0, end = 0; bool on = false; bool valid() const { return end > start; } };
    std::array<Section, arp::kMoods> sections;
    Section section() const { return sections[(size_t) juce::jlimit (0, arp::kMoods - 1, I.mode)]; }
    void setSection (int start, int end);            // a brace (clamped to the idea, at least a 16th)
    void setSectionOn (bool on);
    void clearSection();
    bool sectionActive() const;                      // on, valid, and no Hook Lab audition running
    std::atomic<int> drawSection { 0 };              // DRAG: 0 WHOLE, 1 SECTION
    std::atomic<int> drawOverride { -1 };
    std::array<std::atomic<float>, 16> laneFx {};   // LANE FX: [lane * 4 + k], k = 0 DRIVE, 1 TONE, 2 ECHO, 3 SPACE (all 0 = off; saved with the project)
    void setLaneFx (int lane, int k, float v) { laneFx[(size_t) (juce::jlimit (0, 3, lane) * 4 + juce::jlimit (0, 3, k))] = v; ++version; }
    void setLaneMood (int lane, int mood);          // LANE MOOD: -1 = the idea's; the lane recomposes (one undo step)
    void newLaneVariation (int lane);               // the lane's own new variation (one undo step)
    float getLaneFx (int lane, int k) const { return laneFx[(size_t) (juce::jlimit (0, 3, lane) * 4 + juce::jlimit (0, 3, k))].load(); }            // the loop's own grips: force SECTION (1) / WHOLE (0) for one drag
    int drawMode() const { const int o = drawOverride.load(); return o >= 0 ? o : drawSection.load(); }
    void setPlayCursor (int tick);                   // < 0 or past the idea's end = the beginning
    // TRANSPOSE: a live offset (the "transpose" parameter, automatable, saved with the project, never from presets / moods) on every lane in
    // transposeMask (chords, arp, melody, bass bits) for the preview, the MIDI out, the drag-outs and the Hook Lab; BAKE writes it into the notes
    std::atomic<int> transposeMask { 15 };
    // ---- SOUNDS PER LANE + YOUR SAMPLE (message thread)
    bool linkAll = true;                             // LINK ALL: one sound for everything (as before); off = each lane its own
    bool linkOffByMood = false;
    bool pushFurther = false;                        // PUSH FURTHER for SUNRISE / DUSK / MIDNIGHT (TRAP / HIP HOP always): GENERATE moves away from the last 3 ideas
    std::array<std::vector<arp::RecentIdea>, arp::kMoods> recentIdeas;   // the last <= 3 GENERATEd ideas per mood (session only)
    bool usesPushFurther (int mode) const { return arp::urbanMood (mode) || pushFurther; }
    void rememberIdea (const arp::Idea& X) { auto& r = recentIdeas[(size_t) juce::jlimit (0, arp::kMoods - 1, X.mode)]; r.push_back (arp::recentOf (X)); if (r.size() > 3) r.erase (r.begin()); }                      // TRAP / HIP HOP turned LINK ALL off (their bass has its own 808 / sub): the other moods turn it back on
    int laneChoice (int lane) const;                 // the lane's choice: 0 LINKED, 1..kPresets = a factory sound, kPresets + 1 = YOUR SAMPLE
    void setLaneChoice (int lane, int choice);       // lane -1 (or LINK ALL on): the shared sound (+ every lane linked)
    bool loadSample (const juce::File& f, juce::String& message);    // decode (any rate / channels, capped at 60 s), detect the root, FLAC once, hand over
    bool loadSampleAudio (std::vector<float> x[2], int chans, double rate, const juce::String& name, const juce::String& path, juce::String& message);
    bool restoreSampleFlac (const juce::MemoryBlock& flac, const juce::String& name, const juce::String& path);
    void clearSample();
    bool hasSample() const { return sampleInfo.len > 0; }
    void setSampleParams (const arpsmp::Params& p) { smpP = p; smpBox.write (p); ++version; }
    int sliceSample (float sensitivity = 0.5f);      // SLICE TO NOTES: chop START..END at its transients, switch to SLICES; returns the slice count
    bool slicePattern (int lane);                    // GENERATE PATTERN FROM SLICES: the lane's notes on the slices' own rhythm (one undo step)
    bool decodeSampleBuffer (juce::AudioBuffer<float>& out, double& rate) const;   // the project's own copy (message thread)
    const arpsmp::Params& sampleParams() const { return smpP; }
    struct SampleInfo { int len = 0, chans = 1, root = 60; double rate = 48000, detectedHz = 0; float rootCents = 0, loopS = 0.5f, loopE = 1.0f; juce::String name, path; std::vector<float> peaks; };
    SampleInfo sampleInfo;                           // what the sample panel shows (the audio thread has its own copy)
    juce::MemoryBlock sampleFlac;                    // saved with the project
    juce::StringArray recentSamples() const;
    void auditionSampleRoot (bool on);               // click the waveform: hear it at its root
    std::atomic<bool> forceSampleAudition { false };
    const arpsmp::SampleData* currentSample() const { return sampleHand.peek(); }   // (tests: what the audio thread plays)
    // SYNC: DAW (default: the DAW's play / tempo / position; without a DAW at all the old internal PLAY) | OWN (its own clock, tempo, PLAY / SPACE)
    ghosty::SyncTransport sync;
    bool syncOwn() const { return sync.getMode() == ghosty::SyncTransport::Mode::Own; }
    std::atomic<bool> hostPresent { false };         // a DAW gives us a position (false = standalone / no playhead)
    std::atomic<int> hostLoopTicks { 0 }, hostLoopCount { 0 };   // the DAW keeps jumping back by this much (its loop / pattern length)
    void setSyncOwn (bool own);
    void setOwnBpm (double bpm);
    bool ownTempoSet = false;
    int transposeNow() const { return pTranspose != nullptr ? (int) std::lround (pTranspose->load()) : 0; }
    void setTranspose (int semis);
    void bakeTransposeNow();                         // into the notes + chords (one undo step, the offset back to 0)
    int transposeToKey (int root);                   // TO KEY: the smallest shift (-5..+6) to that root; returns the shift
    int foldedCount() const;                         // notes the current offset folds (to keep them in range)
    arp::Idea exportIdea() const;                    // the idea as it sounds (the live offset baked in): drag-outs
    bool spacePlay();                                // SPACE: start from the cursor / stop; false (does nothing) while the host plays
    int playStartTick() const;
    std::atomic<double> startTickReq { -1.0 };       // the audio thread jumps the free-running transport here

    // ---- MUTANT ARP (message thread; Mutant.h). The cell lives in the idea (I.mutant); every call below is one undo step and
    // realises the ARP lane (and the MELODY lane with TWO LINES). The lanes stay ordinary lanes: the roll, the drag pills, the
    // MIDI out and the tools all see plain notes.
    void mutantGenerate (bool newSeed);                        // GENERATE: a new cell (locked steps kept), or the same seed again
    void mutantMutate (int kind, double amount);               // MUTATE (0), OCTAVE CHAOS (1), RHYTHM CHAOS (2)
    void mutantSetParams (const arp::MutantParams& P, bool regenerate);   // knobs / switches; regenerate = the cell is made again (mode, length)
    void mutantSetActive (bool on);                            // on: the lane becomes the mutant (first time: GENERATE); off: the classic arp engine takes the lane back
    void mutantLockStep (int step, bool on);                   // LOCK NOTES: one step of the cell
    void mutantLockFirst (int n);                              // the first n steps (0 = unlock all)
    void mutantLockSelected (const std::vector<bool>& sel);    // the roll's selected ARP notes -> their steps
    void mutantSeedStep (int dir);                             // SEED < >
    void mutantApply (arp::Idea& X) const;                     // realise X.mutant into X's lanes (no undo, no rebuild: callers are inside change())
    static arp::MutantParams mutantDefaults (int mode, int bpm);   // the knobs a mode starts with
    juce::File writeMutantMidi();                              // DRAG MIDI: the mutant lane(s) as a .mid
    // ---- transport / UI feedback
    std::atomic<bool> internalPlay { false }, followKeys { false };
    std::atomic<double> playTick { -1.0 }, hostBpm { 0.0 };
    std::atomic<bool> hostRunning { false };
    std::atomic<int> heldCount { 0 };
    void tapTempo();

    // ---- LISTEN (sidechain capture -> background analysis)
    void startListening();
    void stopListening();
    void listenToFile (const juce::File& f);
    std::atomic<int> listenState { 0 };   // 0 off, 1 capturing, 2 analysing, 3 result, 4 error
    juce::String listenMessage; listen::Result lastHeard;
    float listenProgress() const;
    void applyHeard (uint32_t seed, bool keepChords);

    // ---- editor prefs (saved with the state)
    juce::String theme = "auto";                     // the look (2026-11-13: only the FACES are offered; a new ArpEZator opens on AUTO = NEON RAIN in MIDNIGHT). Older: "dark" (BLACK) | "purple" | "light" (ghosty::themes::toString) | "ghost" (GHOST, ArpEZator-only)
                                                     // | the FACES (2026-11-11, ArpEZator-only, picture-backed): "holo" | "neon" | "mask" | "dream" | "auto" (a face per mood)
    // the picture faces: 0 = none (BLACK / PURPLE / LIGHT / GHOST), 1 HOLO, 2 NEON RAIN, 3 MASK, 4 DREAM. AUTO picks one per mood:
    // 5 KITSUNE. SUNRISE = DREAM (the watercolour dawn), DUSK = HOLO (the pink holographic city), MIDNIGHT = NEON RAIN (the sign canyon in its puddle), TRAP = KITSUNE (red neon lanterns, the ice-white fox),
    // HIP HOP = MASK (red / mustard / teal stencil)
    static int faceOf (const juce::String& t, int mode)
    {
        if (t == "holo") return 1; if (t == "neon") return 2; if (t == "mask") return 3; if (t == "dream") return 4; if (t == "kitsune") return 5;
        if (t == "auto") { static const int pick[5] { 4, 1, 2, 5, 3 }; return pick[juce::jlimit (0, 4, mode)]; }
        return 0;
    }
    static bool isFace (const juce::String& t) { return t == "holo" || t == "neon" || t == "mask" || t == "dream" || t == "kitsune" || t == "auto"; }
    int faceFor (int mode) const { return faceOf (theme, mode); }
    ghosty::themes::Theme themeIdFor (int mode) const
    {
        if (const int f = faceFor (mode)) return f == 4 ? ghosty::themes::Theme::Light : ghosty::themes::Theme::Dark;   // DREAM paints on paper, the rest at night
        return ghostLook() ? ghosty::themes::Theme::Light : ghosty::themes::fromString (theme);
    }
    ghosty::themes::Theme themeId() const { return themeIdFor (I.mode); }   // (GHOST = glass on the LIGHT base)
    bool ghostLook() const { return theme == "ghost"; }
    static juce::String nextLook (const juce::String& t) { return t == "ghost" ? juce::String ("dark") : isFace (t) ? juce::String ("dark") : ghosty::themes::fromString (t) == ghosty::themes::Theme::Light ? juce::String ("ghost") : ghosty::themes::toString (ghosty::themes::next (ghosty::themes::fromString (t))); }
    static juce::String lookName (const juce::String& t)
    {
        if (t == "ghost") return "GHOST"; if (t == "holo") return "HOLO"; if (t == "neon") return "NEON"; if (t == "mask") return "MASK"; if (t == "dream") return "DREAM"; if (t == "kitsune") return "KITSUNE"; if (t == "auto") return "AUTO";
        return ghosty::themes::displayName (ghosty::themes::fromString (t));
    }
    static juce::String themeFromState (const juce::ValueTree& root);   // 'theme_name', else the old 'theme' key (false/"dark" -> dark, true/"light" -> light, none -> dark)
    bool hintsSeen = false; int selLane = arp::LaneChords;   // opens on CHORDS (2026-11-13: ARP starts muted)
    bool oneTrackDrag = false;

    juce::AudioProcessorValueTreeState apvts;
    // after apvts: destroyed first, so the worker thread stops before the parameters go away
    std::unique_ptr<juce::Thread> worker;
    ghosty::License license { "ArpEZator" };
    // every export asks here (DRAG MIDI / AUDIO, STEMS, Hook Lab tiles, the sample's WAV grip, the loop's WAV render): the demo
    // exports nothing (Shared/ExportGate.h: ui = where the UNLOCK hint shows, id = the path in docs/DEMO-EXPORT-GATE.md)
    bool demoLockForTest = false;
    bool exportAllowed (juce::Component* ui, const char* id) const { return ghosty::exportAllowed (license, ui, id, demoLockForTest); }
    bool exportUnlocked() const { return ! demoLockForTest && ghosty::exportUnlocked (license); }   // silent (paint, timers, writers)
    ghosty::DemoGate demo;

    static juce::var ideaToVar (const arp::Idea& I);
    static void ideaFromVar (const juce::var& v, arp::Idea& I);

private:
    void timerCallback() override;
    void rebuildPattern();
    void publish (ArpPattern* p);
    void flushNotes (juce::MidiBuffer& out, int sample);
    void bypassedBlock (juce::AudioBuffer<float>& buffer, int n, bool own);
    void pushUndo (const juce::String& label);
    int transformPitch (int p, double tick, const ArpPattern& P) const;

    arp::Idea I;
    std::vector<arp::Note> melOverride; bool melOverrideOn = false; int overrideLen = 0;   // HOOK LAB audition (+ its pattern length)
    bool labStartedPlay = false; double labLastTick = -1; int labPasses = 0;   // the lab started the transport / AUTO-PLAY pass counter
    void labSyncHarmony();                           // tiles over other chords: refit the starred ones, drop the rest
    void labPushHistory();
    void labStart (int kind, bool reseed);         // 1 GENERATE, 2 FIND THE MAGIC on the lab worker
    void labDeliver (std::vector<arp::Hook> keep, std::vector<arp::Hook> fresh, int kind, int job, const std::string& print);
    arp::HookParams labParams (const arp::Idea& X) const;
    std::vector<arp::Hook> labRun (const arp::Idea& X, int kind, std::vector<arp::Hook>& keep, const arp::HarvestWeights& W, const std::atomic<bool>* cancel, arp::HookParams hp);
    std::unique_ptr<juce::Thread> labWorker; std::shared_ptr<std::atomic<bool>> labJobCancel; int labJob = 0;
    bool tasteLoaded = false; bool tasteDirty = false; double tasteDirtyAt = 0;
    std::vector<std::pair<arp::HFeat, bool>> tastePending;   // keeps not yet merged into the file
    void learnTaste (const arp::HFeat& f, bool kept);
    static juce::var labToVar (const HookLabState& L);
    static void labFromVar (const juce::var& v, HookLabState& L);
    std::atomic<bool> restartFree { false };         // the lab restarts the free-running transport at bar 1
    float pianoFade = 0;
    ghosty::BypassFader bypass;
    ghosty::BypassFader noteGate;
    ghosty::TransportInfo curT; bool lastOwn = false;
    arpsmp::Handoff<arpsmp::SampleData> sampleHand; arpsmp::ParamBox smpBox; arpsmp::Params smpP;
    void addRecent (const juce::String& path);   // this block's resolved transport (SYNC: DAW | OWN)   // PLAY: ON NOTES (its own fade; "bypassed" = no note held)
    std::atomic<float>* pPlayMode = nullptr; std::atomic<float>* pTranspose = nullptr;
    std::array<std::atomic<float>*, 4> pLanePreset {};                             // PIANO TEST: the sends fade instead of cutting (audio thread)
    struct Snap { juce::String state, label; int bakeT = 0; };   // bakeT: a BAKE TRANSPOSE step (undo brings the offset back)
    int bakeUndoTranspose = 0;
    std::vector<Snap> undoStack, redoStack;
    std::array<juce::String, arp::kMoods> modeStash;   // JSON: { idea, sound } of the modes not on screen
    juce::String soundString() const;
    void stashBeforeReplace (int newMode);
    void restoreSound (const juce::String& xml);
    struct AudEv { int pitch = 60; bool on = false; };
    juce::AbstractFifo audFifo { 64 }; std::array<AudEv, 64> audBuf {};
    std::array<std::atomic<float>*, kLaneVols> pLaneVol {}; std::atomic<float>* pVelScale = nullptr; std::atomic<float>* pCh1 = nullptr;
    std::vector<std::pair<juce::String, uint32_t>> genHist; int genIdx = -1;
    std::vector<double> taps;

    // audio side
    std::atomic<ArpPattern*> pending { nullptr };
    ArpPattern* current = nullptr;
    juce::AbstractFifo retireFifo { 64 }; std::array<ArpPattern*, 64> retired {};
    arpsyn::Engine synth;
    double sr = 44100, freeTick = 0, expectTick = -1; bool wasPlaying = false;
    std::array<std::array<double, 128>, 16> active {};   // MIDI note -> off tick (absolute), < 0 = off
    std::array<bool, 128> held {}; int heldN = 0;
    std::atomic<bool> needFlush { false };
    std::atomic<float>* pSound = nullptr; std::atomic<float>* pVol = nullptr; std::atomic<float>* pPreset = nullptr; std::atomic<float>* pLayerPluck = nullptr;
    std::atomic<float>* pLayerOct = nullptr; std::atomic<float>* pStabs = nullptr; std::atomic<float>* pCut = nullptr; std::atomic<float>* pLfoD = nullptr;
    std::atomic<float>* pLfoR = nullptr; std::atomic<float>* pDelay = nullptr; std::atomic<float>* pVerb = nullptr; std::atomic<float>* pPump = nullptr;
    std::atomic<float>* pKick = nullptr; std::atomic<float>* pMidiOut = nullptr; std::atomic<float>* pFollow = nullptr;
    int chainBase = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);   // async callbacks check it

    // LISTEN capture: the audio thread only copies into this preallocated FIFO
    juce::AbstractFifo capFifo { 1 }; std::vector<float> capBuf; std::atomic<bool> capturing { false }; double capSr = 44100;
    std::atomic<int> capWritten { 0 };
    void analyseAsync (std::vector<float> mono, double rate);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArpProcessor)
};
