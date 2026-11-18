#pragma once
// SPECTRAL LAB host: the canvas + undo + settings, a worker thread that renders previews / ghost sets / split bands /
// morph banks / pitched keys / exports, and a real-time player (MIDI keys, a host-synced loop, region audition, scrub).
// Lives in SpecProcessor. Everything heavy happens on the worker; the audio thread only reads finished buffers.
#include "LabFx.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <deque>

namespace lab {

// single-producer / single-consumer (audio thread) hand-off of immutable objects; the audio thread never frees
template <typename T> class Exchange {
public:
    ~Exchange() { delete pending.load(); delete current; delete retired.load(); }
    void publish(std::unique_ptr<T> p) { collect(); delete pending.exchange(p.release()); }
    T* get() { if (retired.load(std::memory_order_acquire) == nullptr) if (T* p = pending.exchange(nullptr)) { retired.store(current, std::memory_order_release); current = p; } return current; }
    void collect() { delete retired.exchange(nullptr); }
private:
    std::atomic<T*> pending { nullptr }, retired { nullptr }; T* current = nullptr;
};

using Audio = std::shared_ptr<const Stereo>;
constexpr int kPitchRange = 24;               // SPECTRAL keys: re-rendered pitches from -24 to +24 semitones
struct PlayBank {
    Audio main; std::vector<Audio> ghosts, bands, morphs; std::vector<juce::Image> ghostThumbs; std::vector<juce::String> ghostStages;
    Audio morphExact; float morphExactT = -1;                       // the morph rendered at exactly the current MORPH value
    std::array<Audio, 8> laneMorphs {}; std::array<float, 8> laneVals {};   // the 8-bar lane, each bar rendered exactly
    std::array<Audio, 2 * kPitchRange + 1> pitched {};              // the canvas at other pitches, same length (SPECTRAL keys)
    float gain = 1.0f;                                              // AUTO LEVEL
    int version = 0;
};

enum Output { OutCanvas = 0, OutGhost, OutSplit, OutMorph };
enum KeysPitch { PitchSpectral = 0, PitchTape = 1 };
enum StereoIn { InMid = 0, InLeft, InRight, InSide };

// what the processor hands the player every block (automatable parameters)
struct LabParams { float morph = 0, level = 0.8f, width = 0.35f; int output = OutCanvas, ghostHit = 0; uint32_t mutes = 0; };

class LabHost : private juce::Thread, private juce::Timer {
public:
    LabHost();
    ~LabHost() override;
    // ---- audio thread
    void prepare(double sampleRate, int block);
    bool wantsMidi() const { return open.load() && playKeys.load(); }
    void stopAll();                                                 // BYPASS engaged (audio thread): voices, loop, preview, audition, scrub stop
    // transportOut (PLAY: ON NOTES): when given, the part that follows the host transport (the LOOP while the host plays) goes there
    // instead of into out, so the processor can gate it by held notes; nullptr = everything into out (exactly as before)
    void process(juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, double bpm, double ppq, bool hostPlaying, const LabParams& p, juce::AudioBuffer<float>* transportOut = nullptr);
    // ---- message thread: the canvas
    Canvas canvas; Undo undo; Brush brush; ImageMap imageMap; GhostParams ghost; SplitSetup split; ChaosParams chaos;
    std::vector<Sel> locks; std::array<std::atomic<float>, 8> morphLane;
    std::vector<float> sourceAudio, audioA, audioB;     // imported sound (48 kHz), morph A / B
    juce::String sourceName, nameA, nameB;
    juce::Image image;                                   // the picture behind an IMAGE canvas (re-mapped by the IMAGE knobs)
    std::atomic<bool> open { false }, playKeys { true }, oneShot { false }, previewPlaying { false }, laneOn { false }, velMorph { false },
                      keepTransients { true }, autoLevel { true }, loopRegion { false };
    std::atomic<int> output { OutCanvas }, rootNote { 60 }, keysPitch { PitchSpectral }, stereoIn { InMid };
    std::atomic<float> level { 0.8f }, width { 0.35f }, morphNow { 0.0f };
    std::atomic<double> tempo { 120.0 };                            // the project tempo (set by the processor)
    std::atomic<int> sideRev { 0 };                                 // bumps when split / locks / lane change outside the UI (undo, state)
    std::atomic<int> audition { -100 };                             // UI: -1 = hear the sound once, -2 = the region once, >= 0 = ghost variation n
    std::atomic<double> regionStart { -1 }, regionEnd { -1 };       // samples of the sound (-1 = none)
    std::atomic<double> scrubPos { -1 };                            // samples; >= 0 while the ruler is held
    std::atomic<float> playStart { 0.0f }, playEnd { 1.0f };        // START / END of the Lab's own PLAY (fractions of the heard sound; 0 / 1 = the whole loop, as before)
    void newCanvas(double seconds);
    void importAudioAsync(const juce::File& f, std::function<void(bool ok, const juce::String& msg)> done);   // decode + analyse on the worker
    bool importAudio(const juce::File& f, juce::String& err);       // synchronous (tests)
    bool importImage(const juce::File& f);
    void setImage(const juce::Image& img, bool undoable = true);    // a new picture
    void remapImage(bool undoable);                                 // the IMAGE knobs moved: map the same picture again (synchronous)
    void remapImageAsync();                                         // ... on the worker; knob ticks during a re-map are merged into one
    bool loadMorphSound(int which, const juce::File& f, juce::String& err);       // synchronous (tests)
    void loadMorphSoundAsync(int which, const juce::File& f, std::function<void(bool ok, const juce::String& msg)> done);
    void useCanvasAs(int which, std::function<void()> done = {});  // the canvas sound becomes A or B (rendered on the worker)
    // ---- PROCESS page (the shared PROCESS LAB): the Lab's sound is 48 kHz mono
    std::vector<float> processSource();                             // the imported source while the canvas is untouched (bit-exact), else the canvas rendered
    void setProcessed(const std::vector<float>& x, const juce::String& name, bool keepPrevious);   // the result becomes the Lab's source (undoable)
    struct KeptSource { juce::String name; std::vector<float> audio; };
    std::vector<KeptSource> kept;                                   // BOUNCE NEW keeps the sound it came from (this session; the current source is what is saved)
    void useKept(int index);                                        // back to a kept sound (the current one is kept in its place)
    juce::uint32 revision() const { return canvasRev.load(); }
    void edited() { dirty = true; ++canvasRev; dirtyFull = true; }  // canvas changed: preview soon (the worker gets a full copy)
    void editedRange(int t0, int t1) { dirty = true; ++canvasRev; dirtyT0 = std::min(dirtyT0, t0); dirtyT1 = std::max(dirtyT1, t1); }   // only frames t0..t1 changed: only they are copied
    void requestPreview();                                          // copy the canvas to the worker now
    void requestGhost(); void requestSplit(); void requestMorph(); void requestLane();
    void generateChaos();
    void sideEdited();                                              // split / locks / lane changed (state + preview)
    // ---- exports (worker; done() runs on the message thread)
    void sendToBeats(double bpm, std::function<void(juce::StringArray files, juce::String error)> done);
    juce::File dragFile(double bpm, juce::String& why);             // the high-quality file for DRAG WAV OUT, if it's ready
    juce::File dragFileRegion(double bpm, float a, float b, juce::String& why);   // ... only fractions a..b of it (the WaveRegion grip)
    juce::StringArray sendToBeatsNow(double bpm, juce::String& error);   // synchronous (tests)
    std::vector<float> renderHQ() const;                            // offline mid (tests)
    static juce::File sharedSoundsDir();
    static juce::File uniqueFile(const juce::File& dir, const juce::String& base, const juce::String& ext);
    juce::File exportImage(int w = 1600, int h = 800) const;
    // SEND TO ARRANGE: what you hear, rendered on the worker, + a state blob that re-renders it later (+ notes from snapped lines)
    struct ExportSound { struct Note { int pitch = 60; float velocity = 1; double start = 0, length = 0.5; };
                         Stereo audio; float gain = 1, width = 0.35f; int output = OutCanvas; double bpm = 0; juce::String name; juce::MemoryBlock state; std::vector<Note> notes; };
    void renderExport(double bpm, std::function<void(std::shared_ptr<ExportSound>)> done);       // done on the message thread
    static std::shared_ptr<ExportSound> renderExportFromState(const juce::MemoryBlock& state);  // re-render a sound sent earlier (message thread)
    int detectRoot(const std::vector<float>& x) const;
    // ---- status for the UI
    std::atomic<int> bankVersion { 0 }, busy { 0 }, progress { -1 };   // progress 0..100 of an export (-1 none)
    std::atomic<double> playPos { -1.0 };                           // 0..1 of the playing sound (-1 none)
    std::atomic<int> playingGhost { -1 };
    std::atomic<float> meter { 0 }; std::atomic<int> clipCount { 0 };   // output peak (after the limiter); times the limiter had to catch > 0 dBFS
    std::atomic<int> ghostTrimmed { 0 };                            // 1 when GHOST used only the first 8 s
    std::atomic<int> lastPreviewCopyFrames { 0 };                   // frames copied for the last preview (tests: stays small after a ranged edit)
    std::shared_ptr<const PlayBank> latestBank() { const juce::ScopedLock sl(resultLock); return uiBank; }   // message thread, display only
    std::shared_ptr<MorphPrep> getMorphPrep() { const juce::ScopedLock sl(resultLock); return morphPrep; }
    std::function<void(const juce::String&)> onMessage;             // worker messages for the status line (message thread)
    // ---- state
    juce::ValueTree toTree() const;                                 // any thread; uses the cached canvas / audio blocks
    // the same, for a save: keepRendered false (the demo) leaves out the audio the PLUGIN made (a PROCESS result as the source,
    // the canvas rendered into A / B); the user's own imported sounds stay. *stripped counts what was left out.
    juce::ValueTree toTree(bool keepRendered, int* stripped) const;
    void fromTree(const juce::ValueTree& t);                        // any thread: applied now on the message thread, else queued
    juce::MemoryBlock sideState() const;                            // split + locks + lane (for undo)
    void setSideState(const juce::MemoryBlock& m);
    void publishSplit();                                            // after a step / rate / gate edit
    static void sanitise(GhostParams& g); static void sanitise(ChaosParams& c);
private:
    void run() override;
    void timerCallback() override;
    void applyTree(const juce::ValueTree& t);
    std::atomic<int> jobs { 0 };
    enum { JobPreview = 1, JobGhost = 2, JobSplit = 4, JobMorph = 8, JobMorphPrep = 16, JobLane = 32, JobTasks = 64 };
    // the worker keeps its own copy of the canvas; after a ranged edit only the changed frames travel (a patch), never the whole canvas
    struct Patch { int T = 0, srcLen = 0, t0 = 0, t1 = -1; std::vector<Cell> cells; std::vector<float> amp, fq, noise; std::vector<uint8_t> touched;
                   static std::unique_ptr<Patch> of(const Canvas& c, int t0, int t1); void applyTo(Canvas& c) const; };
    std::unique_ptr<Patch> pendingPatch; int dirtyT0 = 1 << 30, dirtyT1 = -1, sentT = -1; bool dirtyFull = true; std::atomic<bool> needFull { false };
    juce::CriticalSection jobLock; std::unique_ptr<Canvas> pendingCanvas; std::vector<float> pendingA, pendingB; GhostParams pGhost; SplitSetup pSplit;
    RenderOpts pOpts; std::array<float, 8> pLane {}; std::deque<std::function<void()>> tasks;
    void snapParams();   // call under jobLock
    void post(std::function<void()> task);
    juce::WaitableEvent wake;
    bool dirty = false; juce::uint32 lastRequestMs = 0;
    std::atomic<juce::uint32> canvasRev { 1 }; juce::uint32 sourceRev = 1, aRev = 1, bRev = 1;
    bool sourceMade = false, aMade = false, bMade = false;   // provenance: true = the plugin made it (saved as "sourceMade" / "aMade" / "bMade"; missing = imported)
    // worker results (worker writes under resultLock, then publishes a PlayBank)
    juce::CriticalSection resultLock; Audio rMain; std::vector<Audio> rGhosts, rBands, rMorphs; std::vector<juce::Image> rThumbs; std::vector<juce::String> rStages;
    Audio rExact; float rExactT = -1; std::array<Audio, 8> rLane {}; std::array<float, 8> rLaneVals {}; std::array<Audio, 2 * kPitchRange + 1> rPitched {};
    float rGain = 1.0f; int bankCounter = 0; juce::uint32 rMainRev = 0;
    std::shared_ptr<MorphPrep> morphPrep; std::shared_ptr<const PlayBank> uiBank;
    void publishBank();
    Exchange<PlayBank> banks; Exchange<SplitSetup> splits;
    std::atomic<float> morphTarget { 0 }; std::atomic<juce::uint64> wantPitch { 0 }; std::atomic<bool> hqWanted { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    juce::String hqKey(juce::uint32 rev, int out, float morphT, double bpm) const;
    bool remapBusy = false, remapAgain = false;
    juce::String hqCacheKey; juce::uint32 workRev = 0; bool workSourceMatch = false; juce::uint32 lastJobMs = 0; float lastTargetSeen = -1; juce::uint32 targetChangedMs = 0, lastExactMs = 0;
    // caches for the state (stateLock) + DRAG WAV OUT (resultLock)
    mutable juce::CriticalSection stateLock; mutable juce::String canvasBlob, sourceBlob, aBlob, bBlob; mutable juce::uint32 canvasBlobRev = 0, sourceBlobRev = 0, aBlobRev = 0, bBlobRev = 0;
    std::unique_ptr<juce::ValueTree> pendingTree;
    std::shared_ptr<Stereo> hqCache;
    struct Snapshot { Canvas canvas; GhostParams ghost; SplitSetup split; int output = 0; float morphT = 0; std::shared_ptr<MorphPrep> prep; double bpm = 120; float gain = 1, width = 0.35f; RenderOpts opts; juce::String base; };
    Snapshot snapshot(double bpm) const;
    static Stereo renderOutput(const Snapshot& s);                  // HQ, what HEAR plays (split / ghost chain / morph at the knob)
    // ---- audio thread state
    double hostRate = 48000; float limGain = 1.0f; int clipHold = 0;
    struct Biq { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; void lowpass(double fc, double q); float tick(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    // band-limited reader for TAPE pitch: the source is low-passed (4th order) below the new Nyquist before it is read faster
    struct BL { long long last = -4; float h[4] {}; Biq f1, f2; bool on = false;
                void setup(double step, double startPos); float read(const std::vector<float>& x, double p); };
    struct Voice { bool on = false, releasing = false; int note = 0; float vel = 1, env = 0; double el = 0, rate = 1, u = 0, start = 0, end = -1; int ghost = 0, pitchSlot = -1; float g[kMaxBands] {}; BL blM, blS; };
    std::array<Voice, 12> voices; int ghostCounter = 0; double previewBeats = 0, previewSamples = 0; long long lastLoopIndex = -1; Voice loopVoice;
    struct Scrub { double head[2] { 0, 0 }; float fade = 1; int active = 0; bool on = false; } scrub;
    void renderVoice(Voice& v, const PlayBank& b, const SplitSetup* s, const LabParams& p, double bpm, double beatsOverride, float morph, bool loop, float& yM, float& yS);
    double heardSeconds(const PlayBank& b, int out) const;
};
} // namespace lab
