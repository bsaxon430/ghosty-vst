// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Engine: one per plug-in instance. Runs the original Samplebrain engine (brain / block /
// search, unmodified) with the realtime renderer port, and keeps every heavy job off the
// audio thread:
//  - a worker thread loads sounds, cuts and analyses them, grows the network (synapses),
//    reads and writes .brain / .samplebrain files, renders WAVs;
//  - finished libraries / targets are handed to the audio thread through a lock-free queue
//    and the old ones come back the same way to be freed on the worker;
//  - small changes (play, pause, switch a sound on/off, delete a sound) are lock-free commands.
// The Qt app did the same work under one mutex, with the audio going silent meanwhile.
#pragma once
#include "Compat.h"
#include "Params.h"
#include "RtRenderer.h"
#include "LiveTarget.h"
#include "Limiter.h"
#include "Shaping.h"
#include "TerrainModel.h"
#include "Tap.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <deque>
#include <list>

namespace mosaic
{
struct SoundItem
{
    juce::String key;      // "mem:..." (the audio lives in compat's memory store)
    juce::String name;     // what the UI shows
    bool enabled = true;
    double seconds = 0;
};

// What the UI draws (made on the worker after each build; never touches the live engine).
struct LibraryPicture
{
    struct Dot { float x, y; int sound; float pos; float level; };
    std::vector<Dot> dots;                    // one per library piece
    std::vector<std::array<int, 4>> links;    // the closest network neighbours (synapses) per piece
    std::vector<juce::String> soundNames;
    int numBlocks = 0, blockSize = 0;
    std::vector<float> bright;                // per piece: its spectral centroid's rank in the library (0 darkest .. 1 brightest)
    std::vector<float> centroidHz;            // per piece: its spectral centroid (Hz)
};
struct TargetPicture
{
    std::vector<float> peaks;                 // per target piece (left)
    std::vector<float> bright;                // per target piece: brightness 0..1
    int numBlocks = 0, hop = 0, blockSize = 0;
    juce::String name;
    double seconds = 0;
};

class Engine : public juce::ChangeBroadcaster
{
public:
    Engine (juce::AudioProcessorValueTreeState& params, bool isEffect);
    ~Engine() override;

    // ---- audio thread
    void prepare (double sampleRate, int maxBlock);
    void process (juce::AudioBuffer<float>& io, juce::MidiBuffer& midi, bool hostPlaying, bool hostInfoValid);
    int latencySamples() const { return m_latency.load() + (P ("limiter") > 0.5f ? TruePeakLimiter::kLatency : 0); }   // (the limiter's look-ahead only while it is on)

    // ---- message thread (all return at once; the work happens on the worker)
    void addFiles (const juce::StringArray& files);
    void addMemorySound (const juce::String& name, const juce::AudioBuffer<float>& audio, double rate);
    void setSoundEnabled (int index, bool on);
    void setAllEnabled (bool on);
    void deleteSound (int index);
    void clearSounds();
    void generateLibrary (bool onlyIfChanged = false);   // onlyIfChanged: skip when the library already holds exactly these sounds and sizes
    void setTargetFile (const juce::File& f);
    void setTargetMemory (const juce::String& name, const juce::AudioBuffer<float>& audio, double rate);
    void generateTarget();
    void clearTarget();
    void generateAll (bool restart);               // GENERATE (the library is rebuilt only if it changed; then it plays)
    void play();
    void pause();
    void restart();                                // back to the top of the target
    bool isPlaying() const { return m_playingUi.load(); }
    void startRecording (const juce::File& f);     // + plays, as the Qt app's record button
    void stopRecording();                          // + pauses
    bool isRecording() const { return m_recorderOn.load(); }
    juce::File lastRecording() const { return m_lastRecording; }

    void loadLibraryFile (const juce::File& f);    // .brain (upstream format)
    void saveLibraryFile (const juce::File& f);
    void loadSessionFile (const juce::File& f);    // .samplebrain (upstream format)
    void saveSessionFile (const juce::File& f);
    void renderToFile (const juce::File& f, double maxSeconds, std::function<void (bool)> done);   // one pass, offline

    // plug-in state: settings + sounds + target (rebuilt identically on load)
    void getState (juce::MemoryBlock& out);
    void setState (const void* data, int size);

    // UI feed
    std::vector<SoundItem> sounds() const { const juce::ScopedLock l (m_modelLock); return m_sounds; }
    juce::String targetName() const { const juce::ScopedLock l (m_modelLock); return m_targetName; }
    std::shared_ptr<const LibraryPicture> libraryPicture() const { const juce::ScopedLock l (m_modelLock); return m_libPic; }
    std::shared_ptr<const TargetPicture> targetPicture() const { const juce::ScopedLock l (m_modelLock); return m_tgtPic; }
    juce::String status() const { const juce::ScopedLock l (m_modelLock); return m_status; }
    float progress() const { return m_progress.load(); }
    bool busy() const { return m_jobsPending.load() > 0; }

    // ---- what the status line shows (message thread)
    // A library build: phase 1 cutting / analysing the sounds, 2 growing the network (the long part:
    // every piece is compared with every other, so it grows with the square of the length).
    struct BuildInfo { int phase = 0; double elapsed = 0, eta = -1; float fraction = 0; int pieces = 0; };
    BuildInfo buildInfo() const;
    double estimateBuildSeconds (double sourceSeconds, int* pieces = nullptr) const;   // on this computer (learns from each build)
    static juce::String timeWords (double seconds);                                    // "about 40 s", "about 2 min"
    juce::String notice() const { const juce::ScopedLock l (m_modelLock); return m_notice; }
    void setNotice (const juce::String& s) { { const juce::ScopedLock l (m_modelLock); m_notice = s; } sendChangeMessage(); }
    bool playPending() const { return m_playPending.load(); }        // GENERATE pressed: plays when the build is done
    bool renderingNow() const { return m_runningUi.load(); }         // the audio thread is playing
    int notesHeld() const { return m_notesUi.load(); }
    bool hostPlaying() const { return m_hostPlayingUi.load(); }
    bool hostSaysTransport() const { return m_hostValidUi.load(); }
    struct Event { juce::uint32 src, tgt, time; juce::uint8 channel; };
    int popEvents (Event* out, int max);
    float outputLevel() const { return m_level.load(); }
    float limiterGain() const { return m_limGain.load(); }
    juce::int64 lateBlocks() const;
    size_t droppedBlocks() const { return m_left.droppedBlocks() + m_right.droppedBlocks(); }
    int currentTargetBlock() const { return m_curTgt.load(); }
    void restBlocks (const std::vector<int>& indices);   // mark pieces as just used (NOVELTY then avoids them)

    // ---- TERRAIN (message thread): the landscape steers the matching (TerrainShaping.h). Flat = untouched.
    // `state` is the page's saved form (both A/B slots); it rides in the plug-in state and beside session files.
    void setTerrain (const ghost::terrain::Terrain& t, const juce::String& state);
    juce::String terrainState() const { const juce::ScopedLock l (m_modelLock); return m_terrainState; }
    Shaping shaping() const { const juce::ScopedLock l (m_modelLock); return m_shapeMsg; }
    struct ShapeNow { bool weights = false, time = false; float novelty = 0, boredom = 0, wild = 0, pos = 0, tilt = 0; };   // tilt: the brightness preference in use (the land's + a sideways drag)
    ShapeNow shapeNow() const;                     // what the audio thread is doing right now (the page's readout)
    // DRAG: the page says how the land is moving while you pull it (0 when you don't): burst 0..1 (speed),
    // side -1..1 (left / right), lift -1..1 (down / up). Heard at once, gone ~0.5 s after you stop; never saved.
    void setDrag (float burst, float side, float lift) { m_dragBurst = burst; m_dragSide = side; m_dragLift = lift; }
    float dragNow() const { return m_shDrag.load(); }
    // MOVING NOW (build 15): the original controls TERRAIN moves, for the screen. Per control: the value you set, the value in use
    // right now (what the engine is really searching with), and whether TERRAIN is moving it. Read-only for the UI: the audio
    // thread publishes with atomics (no lock, no allocation), ~every buffer.
    enum Live { kLiveNovelty, kLiveBoredom, kLiveSmooth, kLiveRepeat, kLiveMix, kLiveRatio, kLiveNRatio, kLiveAlgo,
                kLiveFftStart, kLiveFftEnd, kLiveSynapses, kLiveSlideError, kLiveAutotune, kLiveNMix, kLiveCount };
    struct LiveValue { float set = 0, now = 0; bool moving = false; };
    static int liveIndexOf (const juce::String& paramId);        // -1: TERRAIN doesn't move it
    static const char* liveParamId (int live);
    LiveValue liveValue (int live) const;
    bool soundBenched (int k) const;                             // TERRAIN keeps this sound out of play right now (your own switch is untouched)
    bool soundsMasked() const { return m_playMaskOn.load(); }
    // the host's tempo and position (for MOVEMENT's tempo-synced steps); without them a clock at the last tempo (120 to start with)
    void setHostTempo (double bpm, double ppq, bool ppqValid) { if (bpm > 0) m_hostBpm = bpm; m_hostPpq = ppq; m_hostPpqValid = ppqValid; }
    static void pieceBrightness (const spiralcore::brain& b, double rate, std::vector<float>& rank, std::vector<float>& hz);
    std::function<void (const juce::String&)> onTerrainLoaded;
    void refreshTerrain();                         // message thread (the processor's timer): after a library build   // a session file brought its landscape (message thread)

    // OSC remote control (the Qt app's ports); off unless switched on
    void handleOsc (const juce::String& path, const juce::var& a0);   // any thread
    void setStatus (const juce::String& s);
    std::function<void (const juce::String&, const juce::StringArray&)> onStatusMessage;   // engine /report etc.

    bool isEffect() const { return m_isEffect; }
    double sampleRate() const { return m_sr; }

    // ---- the tap (Tap.h; read-only, studio/TAP.md). Views read mosaic::tap::Model (TapModel.h), not these.
    tap::Tap& tap() { return m_tap; }
    std::shared_ptr<const tap::BrainSnapshot> brainSnapshot() const { const juce::ScopedLock l (m_modelLock); return m_tapBrain; }      // of the newest library built
    std::shared_ptr<const tap::TargetSnapshot> targetSnapshot() const { const juce::ScopedLock l (m_modelLock); return m_tapTarget; }   // of the newest target built

    // tests
    void waitUntilIdle (int timeoutMs = 600000);
    RtRenderer& leftRenderer() { return m_left; }
    LiveTarget* liveTarget() { return m_live; }

private:
    using brain = spiralcore::brain;
    struct PieceInfo { std::vector<float> bright, factor; };   // per library piece (TERRAIN's brightness preference)
    struct World { std::unique_ptr<brain> source; std::unique_ptr<std::list<brain::sound>> graveyard; std::unique_ptr<brain> left, right; };
    struct Swap
    {
        brain* source = nullptr; std::list<brain::sound>* graveyard = nullptr;
        brain* left = nullptr; brain* right = nullptr;
        LiveTarget* live = nullptr; bool setLive = false;
        bool resetRenderers = false;
        bool hasRendererState = false;
        std::unique_ptr<RtRenderer> stateL, stateR;   // from a session file
        bool forcePlay = false, playValue = false;
        PieceInfo* info = nullptr;                    // with a new source
        juce::uint32 brainGen = 0, targetGen = 0;     // (the tap) the snapshots made for this library / target
        ~Swap() { delete info; }
    };
    struct Retired { brain* b = nullptr; std::list<brain::sound>* g = nullptr; LiveTarget* live = nullptr; Swap* swap = nullptr; PieceInfo* info = nullptr; };
    struct Cmd { enum Type { Play, Pause, Restart, Enable, Delete, Clear, Rest } type; int a = 0; int b = 0; };

    // worker
    class Worker;
    std::unique_ptr<Worker> m_worker;
    void post (std::function<void()> job);
    void runJob (std::function<void()>& job);
    std::unique_ptr<brain> buildLibrary (const std::vector<SoundItem>& sounds, double& seconds, bool& refused);
    juce::String librarySignature (const std::vector<SoundItem>& sounds) const;
    void refreshNotice();
    void noticeLongSounds (const juce::String& longestName, double longestSeconds);
    void buildTarget (std::unique_ptr<brain>& l, std::unique_ptr<brain>& r);
    void targetJob();
    void publish (std::unique_ptr<Swap> s);
    void freeRetired();
    template <class F> bool withExclusiveWorld (F&& f);
    void makeLibraryPicture (const brain& b);
    void makeTargetPicture (const brain& b);
    void rebuildLive();
    void scheduleAutoBuild (bool library, bool target);

    // audio side
    void applyParams (bool force);
    void drainQueues();
    void startPlaying (bool fromTop);
    void stopPlaying();
    static void observe (void* ctx, juce::uint32 src, juce::uint32 tgt, juce::uint32 time);

    // the tap (all in Tap.cpp). Audio thread: copies numbers out, never waits, never allocates. Worker: the snapshots.
    tap::Tap m_tap;
    bool tapBegin (int numSamples, int lastNote, bool stereo, int mode, bool hostPlaying, bool hostInfoValid);   // true = someone is reading
    void tapEnd (const float* left, const float* right, int len, float peak);
    void tapResets (int where);
    void tapRested (juce::uint32 piece);
    static void tapBefore (void* ctx);
    static void tapPicked (void* ctx, juce::uint32 src, juce::uint32 tgt, juce::uint32 time, juce::uint32 renderIndex, bool advanced);
    static void tapEnded (void* ctx, juce::uint32 src, juce::uint32 tgt, juce::uint32 time, float pos);
    void tapSnapBrain (const brain& b, const LibraryPicture& pic);
    void tapSnapTarget (const brain& left, const brain& right);
    juce::uint32 m_tapGens = 0, m_tapPendingBrain = 0, m_tapPendingTarget = 0;   // worker only

    juce::AudioProcessorValueTreeState& m_params;
    const bool m_isEffect;
    double m_sr = 44100;
    int m_maxBlock = 512;

    // live (audio thread owns)
    brain* m_source = nullptr;
    std::list<brain::sound>* m_graveyard = nullptr;
    brain* m_tl = nullptr;
    brain* m_tr = nullptr;
    LiveTarget* m_live = nullptr;
    RtRenderer m_left, m_right;
    struct ObsCtx { Engine* e; int ch; } m_obsL { this, 0 }, m_obsR { this, 1 };

    // queues (lock-free for the audio thread)
    juce::AbstractFifo m_swapFifo { 32 }; Swap* m_swapSlots[32] {};
    juce::AbstractFifo m_retireFifo { 64 }; Retired m_retireSlots[64];
    juce::AbstractFifo m_cmdFifo { 512 }; Cmd m_cmdSlots[512];
    juce::CriticalSection m_cmdWriteLock;   // producers only
    void pushCmd (Cmd c);
    juce::AbstractFifo m_evFifo { 4096 }; Event m_evSlots[4096];

    std::atomic<bool> m_exclusiveRequest { false }, m_exclusiveGranted { false }, m_audioBusy { false };

    // params as the engine last saw them
    struct Applied { float v[64]; bool valid = false; } m_applied;
    std::vector<std::atomic<float>*> m_raw;
    std::vector<int> m_rawIndex;
    float P (const char* id) const;
    int pidx (const char* id) const;

    // transport
    std::atomic<bool> m_playingUi { false };
    bool m_running = false;               // renderer playing (audio side)
    float m_fade = 1, m_fadeTarget = 1;
    bool m_stopAfterFade = false;
    int m_notesDown = 0;
    float m_velocity = 1, m_velSmooth = 1;
    bool m_lastHostPlaying = false;
    bool m_livePending = true;
    juce::int64 m_liveOutStart = 0;

    // output
    juce::AudioBuffer<float> m_scratch;
    juce::AudioBuffer<float> m_dryDelay; int m_dryPos = 0;
    TruePeakLimiter m_limiter;
    bool m_limOn = false;                         // audio thread: the limiter is in the signal path (off = not touched at all)
    float m_limFade = 1; int m_limFadeDir = 0, m_limHold = 0;   // (hold: silent until a fresh look-ahead has filled)     // switching it in or out: a 3 ms dip to silence, swapped at a buffer edge
    std::atomic<float> m_level { 0 }, m_limGain { 1 };
    std::atomic<int> m_latency { 0 };              // the effect's live analysis (the limiter's is added while it is on)
    std::atomic<int> m_curTgt { -1 };

    // TERRAIN: the newest shaping (writer: message thread under a spin lock; the audio thread only try-locks)
    juce::SpinLock m_shapeLock;
    Shaping m_shapePosted;
    std::atomic<int> m_shapeGen { 0 };
    int m_shapeSeen = 0;
    Shaping m_shapeTarget, m_shapeLive;          // audio thread: where it is going, and the smoothed one in use
    void updateShaping (int numSamples);
    void updateBias();                           // audio thread: the per-piece factors for the brightness preference
    PieceInfo* m_info = nullptr;                 // audio thread: the live library's
    float m_biasTilt = 0;                        // the tilt the factors were made for (0 = none set)
    // DRAG (message thread -> audio thread): how fast / which way the land is being pulled right now
    std::atomic<float> m_dragBurst { 0 }, m_dragSide { 0 }, m_dragLift { 0 };
    float m_mBurst = 0, m_mSide = 0, m_mLift = 0;   // audio thread: smoothed (fast attack, slow release)
    void applySoundMask();                       // audio thread: your switches AND the landscape's pick
    bool m_userOn[Shaping::kMaxSounds] {};        // audio thread: your own switch per sound (the brain's flags may be masked)
    int m_userCount = 0;
    bool m_maskDirty = false, m_maskApplied = false, m_macrosApplied = false;
    bool m_searchApplied = false, m_netNow = false;    // build 15: TONE / TIMBRE, SHAPE / PITCH ONLY, SEARCH STYLE are being moved
    std::atomic<float> m_liveSet[kLiveCount], m_liveNow[kLiveCount];
    std::atomic<unsigned> m_liveMoving { 0 };
    std::atomic<bool> m_playMaskOn { false };
    std::atomic<juce::uint32> m_playBits[Shaping::kMaxSounds / 32];
    void publishLive();
    void stepWander (int numSamples, bool wander);
    std::atomic<double> m_hostBpm { 120.0 }, m_hostPpq { 0.0 };
    std::atomic<bool> m_hostPpqValid { false };
    float m_wCur[kLiveCount] {}, m_wTgt[kLiveCount] {};
    int m_wTimer[kLiveCount] {};
    double m_wBeats = 0; long m_wStep = -1; juce::uint32 m_wRng = 0x9e3779b9u;
    std::vector<float> soundBrightness() const;
    std::atomic<bool> m_terrainRefresh { false };  // per library sound, as an x position across the land (message thread)
    std::atomic<bool> m_shWeights { false }, m_shTime { false };
    std::atomic<float> m_shNov { 0 }, m_shBor { 0 }, m_shWild { 0 }, m_shPos { 0 }, m_shDrag { 0 }, m_shTilt { 0 };

    // recorder
    juce::TimeSliceThread m_writerThread { "Ghost Mosaic recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> m_writer;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> m_writerPtr { nullptr };
    std::atomic<bool> m_writerInUse { false }, m_recorderOn { false };
    juce::File m_lastRecording;

    // model (message thread + worker, under m_modelLock)
    juce::CriticalSection m_modelLock;
    std::vector<SoundItem> m_sounds;
    juce::String m_targetKey, m_targetName;
    std::shared_ptr<const LibraryPicture> m_libPic;
    std::shared_ptr<const TargetPicture> m_tgtPic;
    std::shared_ptr<const tap::BrainSnapshot> m_tapBrain;
    std::shared_ptr<const tap::TargetSnapshot> m_tapTarget;
    Shaping m_shapeMsg;
    ghost::terrain::Terrain m_terrainMsg;
    juce::String m_terrainState;
    juce::String m_status { "Drop sounds on SOURCE SOUNDS, and a sound on TARGET." };
    juce::String m_notice;                         // a gentle note that stays while it matters (a long sound, the built-in target)
    juce::String m_builtSig;                       // what the live library was built from ("" = nothing / refused)
    std::atomic<int> m_phase { 0 }, m_buildPieces { 0 };
    std::atomic<double> m_buildStartMs { 0 }, m_phaseStartMs { 0 }, m_estAnalyse { 0 }, m_estNetwork { 0 };
    std::atomic<bool> m_playPending { false }, m_runningUi { false }, m_hostPlayingUi { false }, m_hostValidUi { false };
    std::atomic<int> m_notesUi { 0 };
    std::atomic<float> m_progress { 0 };
    std::atomic<int> m_jobsPending { 0 };
    int m_keyCounter = 0;
    juce::String newKey (const juce::String& name);
    std::atomic<bool> m_libDirty { false }, m_tgtDirty { false };
    double m_builtRate = 0;
    float m_builtLib[3] {}, m_builtTgt[3] {};

    JUCE_DECLARE_NON_COPYABLE (Engine)
};
}
