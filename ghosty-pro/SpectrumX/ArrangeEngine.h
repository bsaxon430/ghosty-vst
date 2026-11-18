#pragma once
// Ghosty Spectrum ARRANGE page: the audio side.
//  - processSound(): one clip's sound -> pitched (keep length via Signalsmith Stretch, or tape), stretched, WARPED to the song
//    tempo, reversed, stereo at the engine rate. Cached (Cache) by the clip's processKey, so moving / trimming / fading never
//    re-processes; while a new version is processed the previous one keeps playing (no gaps on tempo changes or edits).
//  - Snapshot: an immutable, fully prepared arrangement (items in samples, buffers ready, morphs pre-rendered, overlaps resolved,
//    envelopes in samples). Built off the audio thread; the audio thread only reads it (no locks, no allocation).
//  - renderItems() / renderMix(): THE renderer. Realtime playback, offline render, stems, bounce and drag-out all call it, so
//    they match sample for sample (the mix is the sum of the per-track stems, in track order).
//  - Session: what the processor owns. Arrangement + undo on the message thread, a builder thread that turns edits into
//    Snapshots, lock-free hand-off to process(), internal transport + host following (in beats: tempo changes stay in sync),
//    background render streamed to files (progress, cancel), drag-out (mix / stems WAV, MIDI for note clips),
//    SEND TO / RECEIVE FROM Ghosty Beats (Documents/Ghosty/Shared Sounds), per-instance re-render registry, thread-safe state.
#include "ArrangeModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace arr {

using Buf = std::shared_ptr<const juce::AudioBuffer<float>>;     // always 2 channels at the engine rate

// ------------------------------------------------------------------ offline DSP (any thread; cancel may be null)
juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& in, double ratio, const std::atomic<bool>* cancel = nullptr);   // ratio = input samples per output sample (band-limited)
juce::AudioBuffer<float> stretchPitch(const juce::AudioBuffer<float>& in, double factor, float semitones, double sampleRate, const std::atomic<bool>* cancel = nullptr);   // length * factor
juce::AudioBuffer<float> processSound(const Payload& p, float pitch, bool keepLength, float stretch, bool reverse, double sampleRate,
                                      double warpRatio = 1.0, const std::atomic<bool>* cancel = nullptr);
// STFT magnitude morph from a to b over n samples (complex-blend phase). Frames near the start are exactly a, near the end exactly b.
// amount 0 = equal-power crossfade, 1 = full spectral morph; smear blurs the spectrum over time around the centre.
void spectralMorph(const float* a, const float* b, float* out, int n, MorphShape shape = MorphShape::SCurve, float amount = 1.0f, float smear = 0.0f);
inline float panLeft(float p) { return juce::jmin(1.0f, 1.0f - p); }    // balance law: centre = unity on both sides
inline float panRight(float p) { return juce::jmin(1.0f, 1.0f + p); }
constexpr double kMicroFadeSeconds = 0.003;          // automatic declick at cut clip edges / loop wraps
constexpr double kOverlapFadeSeconds = 0.005;        // crossfade where a later clip covers an earlier one

// ------------------------------------------------------------------ cache of processed sounds (thread safe)
class Cache {
public:
    Buf get(const Clip& c, double sampleRate, double bpm, const std::atomic<bool>* cancel = nullptr);   // processes on a miss (outside the lock)
    Buf getOrStale(const Clip& c, double sampleRate, double bpm, bool& stale);   // never processes: the exact buffer, else the clip's last one
    Buf find(const juce::String& key);
    void put(const juce::String& key, Buf b);
    void beginPass() { const std::lock_guard<std::mutex> l(m); ++pass; }
    void prune(int keepPasses = 6, size_t maxBytes = kDefaultBudget);   // drops what recent builds didn't use, then oldest over budget
    size_t bytes() const;
    int size() const { const std::lock_guard<std::mutex> l(m); return (int) map.size(); }
    static constexpr size_t kDefaultBudget = (size_t) 256 << 20;
private:
    struct E { Buf buf; int lastPass = 0; };
    mutable std::mutex m; std::map<juce::String, E> map; std::map<juce::String, Buf> latestBase; std::map<juce::uint32, Buf> latestClip; int pass = 0;
};

// ------------------------------------------------------------------ prepared arrangement
struct EnvS { std::vector<juce::int64> t; std::vector<float> v; };   // breakpoints in samples, linear values (gain) or pan
struct Item {
    int track = 0; juce::uint32 clipId = 0; bool morph = false;
    juce::int64 start = 0, end = 0;                                // the samples this item plays [start, end)
    juce::int64 clipStart = 0, clipLen = 0;                        // the clip span the fades are measured in
    juce::int64 bufOffset = 0;                                     // buffer index at clipStart (before the loop wrap)
    juce::int64 fadeIn = 0, fadeOut = 0; Curve inCurve = Curve::Linear, outCurve = Curve::Linear;
    juce::int64 microIn = 0, microOut = 0;                         // automatic declick at cut clip edges (equal power, clip relative)
    juce::int64 edgeIn = 0, edgeOut = 0;                           // crossfades at this piece's own ends (a later clip covering this one)
    bool loop = false; juce::int64 loopX = 0;                      // content loop: read (offset + k) % length, declicked over loopX at the wrap
    float cL = 1, cR = 1;                                          // clip gain x clip pan
    const EnvS* clipEnv = nullptr;                                 // clip gain envelope (clip-relative samples, linear)
    Buf buf;
};
struct TrackS { float tL = 1, tR = 1, gain = 1, panV = 0; const EnvS* vol = nullptr; const EnvS* pan = nullptr; bool used = false; };   // static gains, or automation lanes
struct Snapshot {
    double sampleRate = 48000, bpm = 120; int beatsPerBar = 4; int numTracks = 0;
    std::vector<Item> items;                                       // sorted by start
    std::vector<TrackS> tracks;
    std::deque<EnvS> envs;                                         // storage the items / tracks point into
    juce::int64 maxItemLen = 0, endSample = 0;
    bool loopOn = false; juce::int64 loopStart = 0, loopEnd = 0;
    juce::uint64 generation = 0; bool stale = false;               // stale: some clips play a stand-in until their new version is processed
    double samplesPerBeat() const { return sampleRate * 60.0 / bpm; }
};
struct BuildOptions {
    bool includeInaudible = false; int onlyClipTrack = -1; juce::uint32 onlyClip = 0;
    const std::set<juce::uint32>* onlyClips = nullptr;              // bounce: just these clips
    bool clipLevelOnly = false;                                    // bounce / send: no track gain, pan or automation
    bool allowStale = false;                                       // never process: use stand-ins (fast re-time)
};
// cancel / progress optional; returns nullptr when cancelled
std::unique_ptr<Snapshot> buildSnapshot(const Arrangement& a, double sampleRate, Cache& cache, const BuildOptions& o = {},
                                        const std::atomic<bool>* cancel = nullptr, std::function<void(float)> progress = {});
// ADDS [t0, t0 + n) into L / R (R may equal L: mono). onlyTrack >= 0: that track's stem. Realtime safe.
void renderItems(const Snapshot& s, float* L, float* R, juce::int64 t0, int n, int onlyTrack = -1) noexcept;
// ADDS the mix as the sum of the per-track stems in track order (bit-identical to summing the stems). scratch: 2 x n floats.
// trackPeaks (optional, numTracks floats) receives each track's peak in this block. Realtime safe.
void renderMix(const Snapshot& s, float* L, float* R, juce::int64 t0, int n, float* scratchL, float* scratchR, float* trackPeaks = nullptr) noexcept;

// ------------------------------------------------------------------ whole-arrangement render
struct RenderResult {
    double sampleRate = 48000, bpm = 120;
    juce::AudioBuffer<float> mix;                                  // in-memory renders only (renderArrangement)
    std::vector<juce::AudioBuffer<float>> stems; juce::StringArray stemNames;
    juce::File mixFile; juce::Array<juce::File> stemFiles;         // streamed renders (renderToFiles / Session::startRender)
    bool cancelled = false, failed = false; juce::String error;
    juce::uint64 revision = 0;                                     // Session::audioHash() it was rendered from
    bool loopOnly = false; double fromBeat = 0, toBeat = 0; float peak = 0;
};
struct RenderRequest {
    bool mix = true, stems = false, loopOnly = false;              // loopOnly: the loop region (RENDER LOOP / DRAG LOOP)
    juce::File dir; juce::String base = "Arrange"; int bits = 32;  // 32 = float WAV (no clipping above 0 dBFS)
    double tailSeconds = 0;
};
constexpr double kMaxRenderSeconds = 60.0 * 60.0;
std::shared_ptr<RenderResult> renderArrangement(const Arrangement& a, double sampleRate, Cache& cache, bool stems, double tailSeconds = 0.0,
                                                const std::atomic<bool>* cancel = nullptr, std::function<void(float)> progress = {}, bool loopOnly = false);
std::shared_ptr<RenderResult> renderToFiles(const Arrangement& a, double sampleRate, Cache& cache, const RenderRequest& req,
                                            const std::atomic<bool>* cancel = nullptr, std::function<void(float)> progress = {});
// MIDI for the note-based clips (one MIDI track per arrangement track; positions follow pitch / stretch / warp / reverse / offset / trims / loops)
juce::MidiFile arrangementMidi(const Arrangement& a, bool includeInaudible = false);
int midiNoteCount(const juce::MidiFile& m);
bool writeWav(const juce::File& f, const juce::AudioBuffer<float>& audio, double sampleRate, int bits = 32);
juce::uint64 audioHash(const Arrangement& a, double sampleRate, double bpm, bool loopOnly = false);   // changes only when the sound changes

// ------------------------------------------------------------------ host
struct HostPos {
    bool valid = false, playing = false; juce::int64 timeSamples = 0; double bpm = 0;
    bool hasPpq = false; double ppq = 0;                           // the host's position in beats: the timeline follows this (tempo changes stay in sync)
    bool looping = false, nonRealtime = false;                     // nonRealtime (offline bounce): process() waits for the builder instead of playing stale
};
HostPos hostPos(juce::AudioPlayHead* ph, bool nonRealtime = false);

// ------------------------------------------------------------------ the session (owned by the processor)
class Session {
public:
    Session(); ~Session();

    // ---- message thread: the document
    Arrangement arrangement; History history;
    juce::uint32 selected = 0;                                     // primary selected clip (the inspector)
    std::set<juce::uint32> selection;                              // all selected clips (contains `selected`)
    void changed();                                                // after any edit: rebuild the snapshot in the background
    void commit(const Arrangement& before) { history.push(before); changed(); }   // an undoable edit
    bool undo(); bool redo();
    // CLIP SOURCE entry point: any page hands a sound in. track -1 = selected clip's track or the first empty one;
    // atBeats -1 = playhead (snapped). Returns the new clip id (0 when the payload is invalid).
    juce::uint32 receive(PayloadPtr p, int track = -1, double atBeats = -1);
    // RE-RENDER: per instance. Keep the token; destroying it unregisters (and waits for a running call). Capture a
    // SafePointer / weak_ptr in fn and return nullptr if the page is gone.
    SourceRegistration registerSource(const juce::String& page, Rerender fn) { return registry.add(page, std::move(fn)); }
    PayloadPtr rerender(const SourceRef& ref) { return registry.rerender(ref); }
    bool rerenderClip(juce::uint32 clipId);                        // asks the owning page for a fresh render; every duplicate follows
    bool canRerender(juce::uint32 clipId);                         // the clip has a source page that is registered now (a bounce never has one)
    juce::uint32 bounce(const std::set<juce::uint32>& clipIds, bool replace = true);   // consolidate / bounce in place -> new clip id
    double effectiveBpm() const;                                   // the host's tempo when following it, else the arrangement's
    std::atomic<bool> followHostTempo { true };
    bool hostTempoKnown() const { const double h = hostBpm.load(); return h >= 20 && h <= 400; }

    // ---- audio thread
    void prepare(double sampleRate, int maxBlock);                 // re-renders synchronously when the rate changes (allowed to block)
    void release();
    void process(juce::AudioBuffer<float>& out, const HostPos& host);   // ADDS the arrangement into out (1 or 2 channels)
    float takeTrackPeak(int track);                                // meters (message thread): peak since the last call
    float takeMasterPeak();

    // ---- internal transport (the page's PLAY); stops when the host starts. The LOOP region applies only here.
    void play(); void stop(); bool isPlaying() const { return playing.load(); }
    void setPlayheadBeats(double beats);
    double playheadBeats() const;                                  // what is heard now (host or internal)
    bool hostPlaying() const { return hostIsPlaying.load(); }

    // ---- builder
    juce::uint64 revision() const { return requested.load(); }    // bumps on every changed()
    bool upToDate() const;                                         // the audio thread plays the latest edit (exact, not stand-ins)
    bool waitUntilUpToDate(int ms = 10000) const;
    template <typename F> void withSnapshot(F&& f) { const std::lock_guard<std::mutex> l(retireLock); f(live.load()); }   // peek safely (message thread)
    void housekeeping();                                           // frees retired snapshots, applies a state set from another thread (timer)
    double sampleRate() const { return engineRate.load(); }
    Cache cache;

    // ---- background render, streamed to files
    void startRender(bool stems, std::function<void(std::shared_ptr<RenderResult>)> done);   // done on the message thread
    void startRender(const RenderRequest& req, std::function<void(std::shared_ptr<RenderResult>)> done);
    void cancelRender() { renderCancel.store(true); }
    bool rendering() const { return renderBusy.load(); }
    float renderProgress() const { return renderProg.load(); }
    std::shared_ptr<RenderResult> lastRender, lastStems;           // message thread (files on disk)
    juce::uint64 audioHashNow(bool loopOnly = false) const { return audioHash(arrangement, engineRate.load(), effectiveBpm(), loopOnly); }

    // ---- files
    static juce::File sharedSoundsDir();                           // Documents/Ghosty/Shared Sounds (Ghosty Beats lists it)
    static void setSharedSoundsDir(const juce::File& dir);         // tests: point it somewhere else ({} = default)
    static juce::File dragDir();                                   // temp folder for drag-outs (cleaned of day-old files at start-up)
    static juce::File uniqueFile(const juce::File& dir, const juce::String& base, const juce::String& ext);
    static juce::Array<juce::File> listShared();                   // newest first, any case of extension
    static PayloadPtr loadSound(const juce::File& f, const juce::String& page = "Shared", juce::String* note = nullptr);   // note: "cut to 20 min" etc.
    juce::Array<juce::File> writeAudio(const RenderResult& r, const juce::File& dir, const juce::String& base, bool stems) const;
    juce::File writeMidi(const juce::File& dir, const juce::String& base) const;   // {} when no clip has notes
    // SEND TO BEATS: the selected clip (as heard: processed, faded, gained, no track level) or, with no selection, the mix.
    juce::Array<juce::File> sendToBeats(juce::uint32 clipId, juce::String& error);   // synchronous
    void sendToBeatsAsync(juce::uint32 clipId, std::function<void(juce::Array<juce::File>, juce::String)> done);   // worker -> message thread
    void loadSoundsAsync(const juce::StringArray& files, const juce::String& page, std::function<void(std::vector<PayloadPtr>, juce::StringArray)> done);

    // ---- state (any thread: hosts may save / load off the message thread)
    juce::MemoryBlock getState(bool keepRendered = true, int* stripped = nullptr) const;   // keepRendered false (demo saves): plugin-made sounds left out                            // the last committed arrangement
    bool setState(const void* data, size_t size, juce::String* error = nullptr);   // off the message thread: applied there asynchronously
    bool applyPendingState();                                      // message thread

private:
    void builderLoop(); bool publish(std::unique_ptr<Snapshot> s); void post(std::function<void()> job); void workerLoop();
    std::shared_ptr<Arrangement> latestArrangement();
    std::atomic<Snapshot*> live { nullptr };
    std::atomic<juce::uint64> audioEpoch { 0 }; std::atomic<int> inProcess { 0 };
    std::mutex retireLock; std::vector<std::pair<Snapshot*, juce::uint64>> retired;
    std::atomic<double> engineRate { 48000.0 }, hostBpm { 0.0 };
    std::atomic<bool> playing { false }, hostIsPlaying { false }; bool hostWasPlaying = false;
    std::atomic<juce::int64> playhead { 0 }, heard { 0 }, seekReq { -1 };
    std::atomic<juce::uint64> requested { 0 }, built { 0 };
    std::array<std::atomic<float>, kMaxTracks> trackPeaks {}; std::atomic<float> masterPeak { 0 };
    std::vector<float> scratch; int scratchN = 0;                  // audio thread: 4 x scratchN (track L/R, mix L/R)
    SourceRegistry registry; SourceRegistration fileReg, sharedReg;
    // state shared with host threads
    mutable std::mutex stateM; std::shared_ptr<const Arrangement> committed; std::unique_ptr<Arrangement> pendingState;
    // builder thread
    std::mutex bm; std::condition_variable bcv; std::shared_ptr<Arrangement> pending, last; bool quit = false;
    std::thread builder;
    // worker thread (send / load / bounce jobs)
    std::mutex wm; std::condition_variable wcv; std::deque<std::function<void()>> jobs; bool wquit = false; std::thread worker;
    // render thread
    std::atomic<bool> renderBusy { false }, renderCancel { false }; std::atomic<float> renderProg { 0 };
    std::thread renderThread; std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};

} // namespace arr
