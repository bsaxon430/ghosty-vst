#pragma once
// GHOST SLICE engine: renders a Plan from the ORIGINAL audio (never from earlier renders).
//
//   StretchStream  drives Signalsmith Stretch (MIT, vendored 1.4.0) along a warp map. LIVE (presetCheaper, split computation,
//                  audio thread) and STUDIO (presetDefault, seeded, fixed 512-frame chunks, offline) use this same code, so
//                  their timing agrees. Zero added latency: the source is known in advance, so the stretcher is primed with
//                  outputSeek() and fed ahead of the output by its own latencies.
//   renderPiece    the STUDIO render of one piece: exactly piece.length() frames, bit-exact for the same build + machine.
//   StudioCache    background worker: renders the pieces of the current plan (near the playhead first) into a cache keyed by
//                  studioKey; stale jobs are dropped when their key leaves the plan.
//   Player         the audio-thread mixer: per piece a voice on a path (DIRECT read at 1x / LIVE stretcher / STUDIO buffer);
//                  path changes cross-fade (equal power, 10 ms), LIVE -> STUDIO waits until it is not inside a transient.
//                  Tables are published RCU-style; nothing on the audio thread allocates or locks.
//                  PRIMER: a LIVE stretcher's start (outputSeek + a HOLD ELASTIC's 100 ms 1x pre-roll) costs up to ~1.4 ms, more
//                  than a whole 64-frame block. The audio thread looks ahead (kPrimeAheadS on the timeline, the stopped play
//                  position, the loop start before a wrap) and hands the starts it can see coming to a background thread, which
//                  primes a spare stretcher with exactly the calls the audio thread would make (same start + skip chunks, so the
//                  output is sample-identical). When the voice starts, it adopts the primed stretcher (a pointer move). A start
//                  nobody could see coming (a DAW jump) or a prime not ready in time falls back to priming in the block.
#include "RenderPlan.h"
#include "PitchEdit.h"
#include "signalsmith-stretch/signalsmith-stretch.h"
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <condition_variable>
#include <functional>

namespace gslice
{
// reads a piece's (mirrored) WF coordinates from the working audio
struct PieceReader
{
    const WorkingAudio* w = nullptr; bool reverse = false; double mirror = 0;
    inline float at (int ch, int64 p) const noexcept { return w->at (ch, reverse ? (int64) std::llround (mirror) - 1 - p : p); }
};

class StretchStream
{
public:
    enum class Preset { Live, Studio };
    explicit StretchStream (long seed = 0x51ce) : st (seed) {}
    void configure (Preset p, double sampleRate, int maxChunk);
    // prime at region-local output frame u (the next render() starts exactly there)
    // curve: GHOST PITCH note edits (time-varying shift on top of pitchSemis, read one output latency ahead every 128 frames)
    void start (const WarpMap& map, const PieceReader& rd, int64 u, double pitchSemis, double formantSemis, double tonalityHz = 8000, double formantBaseHz = 0,
                const PitchCurve* curve = nullptr);
    // smoothed by the stretcher's own block overlap. tonalityHz: the tonality limit; formantBaseHz: the voice's f0 (0 = guess)
    void setPitch (double pitchSemis, double formantSemis, double tonalityHz = 8000, double formantBaseHz = 0);
    // the same stream in a newer table (equal map): point at that table's copies (the old table may be freed)
    void rebind (const WarpMap& m, const PitchCurve* c) noexcept { map = &m; curve = c; }
    static constexpr int kCurveStep = 128;
    // n <= maxChunk frames of output from the current position
    void render (float* L, float* R, int n);
    // render n frames and drop them (a pre-roll: the stretcher settles on the audio before the piece, e.g. a HOLD join). n is split
    // into maxChunk blocks; no allocation
    void skip (int64 n);
    // a phase steer (HOLD ELASTIC, STUDIO): an extra transposition shaped as a raised-cosine bump of peak `cents` over region-local
    // output [a, b). Its integral moves the output's phase like a time shift, so the stretched sound can meet the source in phase
    // at a join. 0 = none
    void steer (int64 a, int64 b, double cents) { steerA = a; steerB = b; steerCents = cents; }
    int64 position() const { return u; }
    int latencyIn() const { return st.inputLatency(); }
    int latencyOut() const { return st.outputLatency(); }
private:
    signalsmith::stretch::SignalsmithStretch<float> st;
    const WarpMap* map = nullptr; PieceReader rd; const PitchCurve* curve = nullptr;
    std::vector<float> in[2], out[2];
    int64 inPos = 0, u = 0; int cap = 0, maxN = 0; double sr = 48000, curPitch = 1e9, curFormant = 1e9, curTon = 0, curBase = -1;
    double basePitch = 0, baseFormant = 0, baseTon = 8000, baseHz = 0;   // the piece's own settings (the curve adds to them)
    int64 steerA = 0, steerB = 0; double steerCents = 0, curSteer = 0;
    bool split = false;
    double steerAt (int64 at) const noexcept;
    void fill (int64 from, int n);
    void applyPitch (double p, double f, double ton, double base);
    void applyCurve (int64 at);
    void renderChunk (float* L, float* R, int n);
    struct Ptrs { float* p[2]; float* operator[] (int c) const { return p[c]; } };
};

struct StudioBuffer
{
    uint64 key = 0;
    juce::AudioBuffer<float> audio;      // 2 x piece length, BEFORE gain / fades (the stream itself)
    std::vector<uint8_t> hot;            // per 1 ms frame: inside +-5 ms of a transient (no LIVE -> STUDIO switch there)
    int hotFrame = 48;
    bool isHot (int64 i) const { const auto k = (size_t) juce::jmax<int64> (0, i / juce::jmax (1, hotFrame)); return k < hot.size() && hot[k] != 0; }
};

class StudioCache;
namespace render
{
    // the stream of one piece (no gain / fades), exactly piece.length() frames. cancel: polled between chunks (true = abandon)
    std::shared_ptr<const StudioBuffer> piece (const Piece& pc, const WorkingAudio& w, double pitchSemis, double formantSemis,
                                               const std::function<bool()>& cancel = {});
    void markTransients (StudioBuffer& b, double sampleRate);
    void spliceTransients (const Piece& pc, const WorkingAudio& w, int64 u, float* L, float* R, int n);
    // the whole plan: every piece rendered STUDIO (from the cache when it has it), enveloped and summed. ceiling: the
    // CleanStage true-peak ceiling (latency compensated) like the plugin's output.
    juce::AudioBuffer<float> plan (const Plan& p, const WorkingAudio& w, StudioCache* cache, bool ceiling);
}

class StudioCache
{
public:
    StudioCache();
    ~StudioCache();
    struct Job { Piece piece; double pitch = 0, formant = 0; int64 distance = 0; };
    // replace the wanted set (message thread): jobs not yet done are queued nearest first; keys not wanted any more are dropped
    void request (std::shared_ptr<const WorkingAudio> w, std::vector<Job> jobs);
    std::shared_ptr<const StudioBuffer> get (uint64 key) const;
    void put (std::shared_ptr<const StudioBuffer> b);
    size_t pending() const;
    // STUDIO progress of the current request: renders done / wanted (1 = everything the plan needs is rendered)
    float progress() const { const int r = requested.load(); return r <= 0 ? 1.0f : juce::jlimit (0.0f, 1.0f, 1.0f - (float) pending() / (float) r); }
    bool waitIdle (int timeoutMs);       // tests / offline export: until the queue is empty
    std::function<void()> onRendered;    // called from the worker after each finished render
    void clear();
    size_t bytes() const;
    size_t maxBytes = (size_t) 1536 << 20;
private:
    void run();
    mutable std::mutex m; std::condition_variable cv;
    std::map<uint64, std::shared_ptr<const StudioBuffer>> done;
    std::vector<Job> queue; std::set<uint64> wanted; std::shared_ptr<const WorkingAudio> work;
    std::atomic<uint64> busyKey { 0 };
    std::atomic<int> requested { 0 };
    bool quit = false; bool busy = false;
    std::thread worker;
};

// ---------------------------------------------------------------- the real-time player
struct PlayTable
{
    uint64 gen = 0;
    Plan plan;
    std::shared_ptr<const WorkingAudio> work;
    std::vector<std::shared_ptr<const StudioBuffer>> studio;   // per piece (may be null)
    bool useStudio = false;                                    // QUALITY: STUDIO (play renders when ready) / LIVE
};

class Player
{
public:
    static constexpr int kVoices = 24, kStretchers = 16, kChunk = 256, kPrimes = 6;
    static constexpr double kPrimeAheadS = 0.15;              // how far ahead the audio thread asks for primed stretchers
    Player();
    ~Player();
    void prepare (double sampleRate, int maxBlock);          // allocates (not the audio thread)
    // message thread: publish a new table (the audio thread picks it up at its next block)
    void publish (std::shared_ptr<PlayTable> t);
    void collectGarbage();                                   // message thread: frees tables the audio thread no longer uses
    std::shared_ptr<PlayTable> currentTable() const { return keep.empty() ? nullptr : keep.back(); }
    size_t tablesKept() const { return keep.size(); }
    // audio thread: ADD the output for timeline frames [T, T+n) to L/R. playing = false: every voice fades out.
    void render (float* L, float* R, int n, int64 T, bool playing) noexcept;
    // OFFLINE BOUNCE (non-realtime only, from the audio thread): pull finished STUDIO renders into the current table, and count
    // the stretched pieces in [T, T + n) still without one. (StudioCache::get takes its mutex: never call these in real time.)
    void fillStudio (const StudioCache& cache) noexcept;
    int missingStudio (int64 T, int64 n) const noexcept;
    uint64 currentGen() const noexcept { const auto* t = current.load (std::memory_order_acquire); return t != nullptr ? t->gen : 0; }
    // audio thread, before render(): where playback continues next if not at T + n (the stopped play position, the loop start
    // shortly before a wrap); -1 = nowhere else. The LIVE stretchers for it are primed in the background.
    void setJumpHint (int64 hf) noexcept { jumpHint = hf; }
    // tests: wait until no prime is queued or running (true) / timeout
    bool waitPrimesIdle (int timeoutMs) const;

    // telemetry (any thread)
    std::atomic<int> activeVoices { 0 }, liveStretchers { 0 }, studioVoices { 0 }, voicesStarved { 0 }, switches { 0 }, switchesDeferred { 0 };
    std::atomic<int64> lastPos { 0 };
    std::atomic<int> grainVoices { 0 };
    std::atomic<int> primesAdopted { 0 }, primesMissed { 0 };   // starts that used a primed stretcher / primed in the block
    double xfadeMs = 10.0, releaseMs = 5.0;
private:
    enum Kind { None = 0, Direct, Live, Studio, Grain };   // Grain = a HOLD's grain schedule (SUSTAIN, and the TEXTURE / FREEZE preview)
    struct Path { int kind = None; int st = -1; int64 u = 0; };
    struct Voice
    {
        bool on = false, releasing = false; uint64 key = 0; const PlayTable* table = nullptr; int pi = -1;
        Path cur, old; int xfPos = 0, xfLen = 0; int relPos = 0, relLen = 0; float gNow = 1.0f;
        int64 nextU = 0;
    };
    Voice voices[kVoices];
    std::unique_ptr<StretchStream> stretchers[kStretchers]; bool stBusy[kStretchers] {};
    std::vector<float> tmpA[2], tmpB[2];
    double sr = 48000; int maxBlock = 512; float gainCoef = 0.002f;
    std::atomic<PlayTable*> current { nullptr };
    std::atomic<uint64> minGenInUse { 0 };
    std::vector<std::shared_ptr<PlayTable>> keep;            // message thread
    uint64 genCounter = 0;
    // a host that stops calling the audio callback: after 1 s without a render the voices are dropped (the next render does it
    // before it touches them) so old tables (and the STUDIO buffers they hold) can be freed. Dekker pair inRender / dropVoices.
    std::atomic<bool> inRender { false }, dropVoices { false };
    std::atomic<uint32_t> renderCount { 0 };
    uint32_t gcSeenCount = 0; double gcSeenMs = 0;

    // PRIMER slots: Free -> Requested (audio) -> Working -> Ready (worker) -> adopted / cancelled -> Free (audio)
    enum PrimeState { PFree = 0, PRequested, PWorking, PReady };
    struct Prime
    {
        std::atomic<int> state { PFree }; std::atomic<bool> cancel { false };
        int st = -1, pi = -1; uint64 key = 0; int64 u = 0, pre = 0; const PlayTable* table = nullptr; bool wanted = false;
    };
    Prime primes[kPrimes];
    int64 jumpHint = -1;
    std::atomic<bool> primeQuit { false };
    std::thread primeThread;
    void primeLoop();
    void primeAhead (const PlayTable* t, int64 T, int n, bool playing) noexcept;
    void wantPrime (const PlayTable& t, int pi, int64 u) noexcept;
    void releasePrime (Prime& pr) noexcept;
    void dropPrimes (bool wait) noexcept;

    int allocStretcher() noexcept;
    void freePath (Path& p) noexcept { if (p.st >= 0) { stBusy[p.st] = false; p.st = -1; } p.kind = None; }
    bool startPath (Path& p, int kind, const PlayTable& t, const Piece& pc, int64 u) noexcept;
    void renderPath (Path& p, const PlayTable& t, const Piece& pc, float* L, float* R, int n) noexcept;
    int desiredKind (const PlayTable& t, int pi) const noexcept;
    Voice* findVoice (uint64 key) noexcept;
    Voice* freeVoice() noexcept;
    void renderVoice (Voice& v, const PlayTable& t, float* L, float* R, int off, int n, bool releaseOnly) noexcept;
};
} // namespace gslice
