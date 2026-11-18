// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// The plug-in / app (Brandon's layout, 2026-11-05; build 2: three engines, DspEngine.h):
//   ENGINE         SEED-VC (AI re-sing) / NATURAL (keeps the performer) / CLASSIC (re-synthesised) / BEST OF ALL (SEED-VC near
//                  A and B, NATURAL in the middle, crossfaded). While SEED-VC sings, NATURAL's render (~100x faster) is the
//                  instant preview (INSTANT PREVIEW, on by default). Takes of NATURAL / CLASSIC / BEST OF ALL are measured
//                  with the same CAMPPlus as SEED-VC's when the voice engine is installed (otherwise: "not measured").
//   IDENTITY LAB   voice A + voice B -> voice C: the blend (BALANCE), NOVELTY + VARIATION, Seed-VC's own controls. CREATE NEW
//                  VOICE C sings a short preview with it and MEASURES it (CAMPPlus distance to A and B; the UNIQUE badge only
//                  when the measurement says so). SAVE VOICE C keeps it in the VOICE LIBRARY.
//   SING ENGINE    a performance (your melody, recorded here, or an audio guide file) sung by voice C. The SPIRIT BOARD puck is
//                  the same balance; MOTION orbits it in time with the song. Then PLAY and drag the WAV out.
//   TOOLS          TERRAIN: the land moves Seed-VC's own controls.
//   SETTINGS       the engine (install / where it is / CPU only), the look, the credits.
//   CHOP (build 8)  the instrument: the clip, its edits, AUTO CHOP, the keys (INTERSECT's engine plays it from MIDI), CONVERT VOICE.
// Build 8: Ghost Voice is an INSTRUMENT (FL Studio's channel rack). The output is the CHOP page's sound plus PLAY's take;
// the input is only recorded (REC), never passed through.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VoiceEngine.h"
#include "DspEngine.h"
#include "VoiceTerrainMap.h"
#include "SpiritBoard.h"
#include "TerrainInteraction.h"
#include "GhostThemes.h"
#include "ChopEngine.h"

namespace gv
{
enum Slot { slotA = 0, slotB = 1, slotPerf = 2, kSlots = 3 };
juce::String slotName (int s);

// what a take is made of (the comparisons only look at what the engine uses: SEED-VC's settings, the DSP settings, or both)
struct Recipe
{
    Engine engine = Engine::seedvc;
    float balance = 0.5f;
    EngineSettings seed;       // SEED-VC's settings (TERRAIN's moves in); seed.semitones is also the DSP engines' PITCH
    DspSettings dsp;           // NATURAL / CLASSIC (TERRAIN's moves in)
    bool usesSeed() const { return engine == Engine::seedvc || (engine == Engine::best && seedWeight (balance) > 0.0f); }
    bool usesDsp() const { return isDsp (engine) || (engine == Engine::best && seedWeight (balance) < 1.0f); }
    bool same (const Recipe&) const;
};

// a voice C kept in the library: its recipe (the takes are copied into its folder), what was measured, its preview
struct SavedVoice
{
    juce::String name;
    juce::File dir;                // <workDir>/library/<name>
    juce::Time created;
    juce::StringArray a, b;        // the takes (inside dir)
    float balance = 0.5f;
    EngineSettings settings;       // the controls as set
    EngineSettings rendered;       // as the preview was rendered (TERRAIN's moves in)
    Identity identity;
    juce::File preview;
    Engine engine = Engine::seedvc;    // voices saved before build 2 were SEED-VC
    DspSettings dsp, dspRendered;
    static bool load (const juce::File& dir, SavedVoice& out);
};

class Processor : public juce::AudioProcessor, private juce::Timer
{
public:
    Processor();
    ~Processor() override;

    // ---- the session (message thread)
    juce::StringArray takes[kSlots];          // A and B: one or more takes; PERFORMANCE: one file
    bool rights = false;                      // "I have the rights to these voices"
    EngineSettings settings;                  // Seed-VC's controls + NOVELTY / VARIATION
    Engine engine = Engine::best;             // the ENGINE picker (BEST OF ALL: what Brandon liked in the blind test)
    DspSettings dsp;                          // NATURAL / CLASSIC's controls (TONE / TIMBRE / TEXTURE / BREATH, KEEP MY CONSONANTS)
    bool instantPreview = true;               // NATURAL's render is heard while SEED-VC sings
    ghosty::spirit::Board board;              // ghost A (0), ghost B (1), the puck (= BALANCE), MOTION (board.orbit)
    ghost::terrain::TerrainEditor terrain;
    float terrainAmount = 1.0f;
    ghosty::themes::Theme theme = ghosty::themes::Theme::Dark;     // build 13: opens DARK; LIGHT (SPLASH) or DARK; saved as "look"
    juce::String voiceName { "Voice C" };
    bool bubbles = true;                      // BUBBLES on presses (SETTINGS)

    // voice C (IDENTITY LAB)
    juce::File preview;                       // the last CREATE NEW VOICE C preview
    Identity previewId;                       // what CAMPPlus measured on it
    float previewBalance = -1;                // the balance it was made at (-1 = none yet)
    EngineSettings previewSettings;
    Engine previewEngine = Engine::seedvc;
    DspSettings previewDsp;
    bool previewQuick = false;                // the preview is NATURAL's instant stand-in while SEED-VC sings
    juce::var analysisAB;                     // ANALYZE VOICES (CAMPPlus on A and B)
    // the song (SING ENGINE)
    double previewSeconds = 0;                // how long the last preview took to make
    juce::String lastError;                   // why the last CREATE / SING failed (empty = it didn't)
    juce::File result;                        // the last song take (PLAY, drag out)
    juce::String resultInfo;
    Identity resultId;

    float blendB() const;                     // BALANCE: B's share at the puck
    void setBalance (float b);                // puts the puck on the A..B line
    float blendAt (double seconds) const;     // with MOTION (and TERRAIN's lean) at that time of the take
    bool motionOn() const { return board.orbit.mode != ghosty::spirit::Orbit::Off; }
    double bpm() const { return hostBpm.load(); }
    EngineSettings effectiveSettings() const; // the controls with TERRAIN's moves
    DspSettings effectiveDsp() const;         // NATURAL / CLASSIC's controls with TERRAIN's moves
    Recipe currentRecipe() const;             // what CREATE would make now
    Recipe previewRecipe() const;             // what the preview was made with
    bool noveltyWorks() const { return engine == Engine::seedvc || engine == Engine::best; }
    juce::String noveltyWhy() const;          // why NOVELTY is greyed out (empty = it works)
    Engine dspEngineFor (Engine e) const { return e == Engine::classic ? Engine::classic : Engine::natural; }
    VoiceLand land() const;
    bool previewIsCurrent() const;            // the preview was made with the current balance + settings

    // ---- CHOP (build 8): the instrument
    std::unique_ptr<chop::ChopEngine> chopper = std::make_unique<chop::ChopEngine>();

    // ---- the engine
    Runner runner;                            // SEED-VC (the helper): renders, ANALYZE, MEASURE, install
    DspRunner dspRunner;                      // NATURAL / CLASSIC (built in)
    enum class Task { none, preview, song, analyze, install, measure };
    Task task() const { return currentTask; }
    bool busy() const { return runner.busy() || dspRunner.busy() || currentTask != Task::none; }
    void cancelAll();
    juce::String progressText() const;        // what is happening now (both engines)
    float progressValue() const;
    Job makePreviewJob() const;               // the guide = the performance, or voice A's first take when there is none
    Job makeSongJob() const;
    std::vector<float> seedMixesFor (const std::vector<float>& ladder) const;   // BEST OF ALL: the ladder steps SEED-VC must sing
    juce::String whyNotPreview() const;
    juce::String whyNotSong() const;
    bool createVoiceC();
    bool renderSong();
    bool analyzeVoices();
    bool installEngine();
    std::function<void()> onChange;
    std::function<bool()> engineReady = [] { return engineInstalled(); };   // tests swap in their stand-in engine
    double fakeEngineDelay = 0;               // tests: the stand-in engine waits this long (so the instant preview can be seen)

    // ---- library
    std::vector<SavedVoice> library;
    void reloadLibrary();
    bool saveVoiceC (const juce::String& name, juce::String& why);
    void useVoice (int i);                    // loads it into the lab (and the song page uses it)
    void deleteVoice (int i);

    void addTakes (int slot, const juce::StringArray& files);
    void clearSlot (int slot);
    double takeSeconds (int slot) const;

    // ---- recorder (any slot, from the input)
    void startRecording (int slot);
    void stopRecording();
    int recordingSlot() const { return recSlot.load(); }
    double recordedSeconds() const;

    // ---- preview player: any file (a slot's take, voice C's preview, the song)
    void play (const juce::File&);
    void stopPlaying();
    bool isPlaying (const juce::File& f) const { return playing.load() && playingFile == f; }
    bool isPlaying() const { return playing.load(); }
    float playPosition() const;               // 0..1

    // ---- AudioProcessor
    const juce::String getName() const override { return "Ghost Voice"; }
    void prepareToPlay (double sr, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }   // INTERSECT's longest release
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);

    static constexpr double kMaxRecordSeconds = 300.0;
    static constexpr double kPreviewSeconds = 12.0;   // the preview sings at most this much of the guide

private:
    void timerCallback() override;
    void finished();                          // the SEED-VC runner finished
    void dspFinished();                       // the DSP runner finished
    void finalise();                          // both parts are in: make the preview / the take
    void startMeasure (const juce::File& f, float mix);
    void changed() { if (onChange) onChange(); }
    std::atomic<double> hostBpm { 120.0 };
    double sampleRate = 48000.0;
    Task currentTask = Task::none;
    std::vector<float> recBuf; std::atomic<int> recPos { 0 }, recSlot { -1 }; std::atomic<bool> recArmed { false };
    std::vector<float> playBuf; int playFs = 44100; juce::File playingFile;
    std::atomic<double> playPos { 0 }; std::atomic<bool> playing { false };
    Job lastJob;
    mutable std::map<juce::String, double> lengthCache;   // takeSeconds: file|time -> seconds
    // the task in flight: which parts it needs, what came back
    struct Pending
    {
        Recipe recipe; bool song = false;
        bool needSeed = false, needDsp = false, seedDone = false, dspDone = false, seedOk = false, dspOk = false;
        std::vector<float> seedMixes, dspMixes;
        std::map<float, juce::File> seedRes, dspRes;
        std::map<float, Identity> seedIds;
        juce::File outDir; juce::String perfName; double t0 = 0;
        juce::String error;
    } pend;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
}
