// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// THE TAP: how the screen learns what the engine is doing without touching it (studio/TAP.md,
// studio/ENGINE_MAP.md part 3, C1 / C2). Two halves:
//
//  1. cells: 32-byte plain records the AUDIO thread writes into one lock-free queue while a reader is
//     attached (full = the record is dropped whole and counted; the audio thread never waits, never
//     allocates, never takes a lock). One writer (audio), one reader (the screen's model).
//  2. snapshots: read-only copies of every piece's measurements, made by the WORKER before a library /
//     target is handed to the audio thread, numbered, and published under the engine's model lock.
//
// The tap only copies numbers out. Nothing here is read back by the engine.
#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace mosaic::tap
{
// ------------------------------------------------------------------------------------ cells
// Bulk kinds (>= kWeights) are a Cell header whose `more` data cells follow; every other kind is one cell,
// and a pick is always kMatch immediately followed by its kUsed (pushed as one record).
enum Kind : uint8_t { kMatch = 1, kUsed, kEnd, kReset, kMeter, kShape, kClock, kParamsA, kParamsB, kTransport, kWorld, kGap, kRest,
                      kWeights = 32, kSounds, kUsage, kLiveBlock };
inline bool isBulk (uint8_t kind) noexcept { return kind >= kWeights; }

struct Cell      { uint8_t kind, a; uint16_t more; uint32_t w[7]; };                                   // generic view, and the header of the bulk kinds
struct Match     { uint8_t kind, ch, algo, flags; uint32_t src, tgt, time, prevSrc, renderIndex; float novelty, boredom; };
struct Used      { uint8_t kind, ch; uint16_t pad; float nextBefore, srcAfter, nextAfter; uint32_t synapses, spare; double error; };
struct End       { uint8_t kind, ch; uint16_t pad; uint32_t src, tgt, time; float lastPos; uint32_t spare[3]; };
struct Reset     { uint8_t kind, ch, where, pad; uint32_t count; uint32_t spare[6]; };
struct Meter     { uint8_t kind, flags; uint16_t buffers; uint32_t clock; float peak, limGain, fade, velGain; uint32_t lost, parked; };
struct Shape     { uint8_t kind, pad[3]; float minL, maxL, minR, maxR; uint32_t spare[3]; };
struct Clock     { uint8_t kind, ch; uint16_t pad; uint32_t renderTime; float targetTime; uint32_t active, droppedBlocks; float keyPitch; uint32_t spare[2]; };
struct ParamsA   { uint8_t kind, algo; uint16_t flags; uint32_t fftStart, fftEnd, stretch, synapses; float ratio, nRatio, stickyness; };
struct ParamsB   { uint8_t kind, pad[3]; float autotune, nMix, targetMix, volume, slideError, biasTilt, dry; };
struct Play      { uint8_t kind, playMode, flags, notesDown; float velocity, bpm; int32_t lastNote; double ppq; uint32_t spare[2]; };
struct World     { uint8_t kind, flags; uint16_t pad; uint32_t brainGen, targetGen, srcBlocks, tgtBlocks, hop, srcLen; int32_t liveBase; };
struct Gap       { uint8_t kind, pad[3]; uint32_t lostCells; uint32_t spare[6]; };
struct Rest      { uint8_t kind, flags, pad[2]; uint32_t piece; float usageAfter; uint32_t spare[5]; };
// bulk headers (a Cell): kWeights  more = 13, then 100 floats (TERRAIN's weight per band)
//                        kSounds   a = 1 when the list was cut short; w[0] = sounds in the library now, w[1] = brainGen, w[2] = sounds listed;
//                                  then two uint32 per sound in the engine's order: its first piece (top bit set = switched on, by you
//                                  AND by TERRAIN), and the piece its search stops before (brain::sound m_end: its last piece)
//                        kUsage    w[0] = first piece, w[1] = count (<= 56), w[2] = brainGen; then the engine's "used" counters of those pieces
//                        kLiveBlock a = channel, w[0] = target piece, w[1] = its dominant frequency (a float's bits), w[2] = bands it has;
//                                  then 224 floats as the engine holds them: 100 bands, 12 timbre, 100 flattened bands, 12 flattened timbre
// A pick is always kMatch immediately followed by its kUsed, and for a live target (when asked for) by its kLiveBlock.

namespace matchFlag  { enum : uint8_t { movesOn = 1, errorValid = 2, nothingFound = 4, nextExists = 8, liveTarget = 16, prevUnknown = 32, liveBlock = 64 }; }
namespace meterFlag  { enum : uint8_t { running = 1, limiterOn = 2, stereo = 4, live = 8 }; }
namespace paramFlag  { enum : uint16_t { weightsOn = 1, factorOn = 2, timeShaped = 4, maskOn = 8, terrainStyle = 16, stereo = 32 }; }
namespace playFlag   { enum : uint8_t { wish = 1, running = 2, hostPlaying = 4, hostReports = 8, stopAfterFade = 16, recording = 32, livePending = 64 }; }
namespace worldFlag  { enum : uint8_t { hasLibrary = 1, hasTarget = 2, live = 4, effect = 8 }; }
namespace restFlag   { enum : uint8_t { someLost = 1 }; }

inline constexpr int kBands = 100, kTimbre = 12, kFeatures = 2 * (kBands + kTimbre);   // per piece: bands + timbre, plain and flattened
inline constexpr int kWeightCells = 13;                                                // 100 floats behind a kWeights header
inline constexpr int kLiveBlockCells = 28;                                             // 224 floats behind a kLiveBlock header
inline constexpr int kUsageFloats = 56, kUsageCells = 7;                               // one slice of the engine's "used" counters
inline constexpr int kMaxSoundsSent = 512, kSoundCells = kMaxSoundsSent / 4;           // more sounds than this: listed short, and flagged
inline constexpr int kMaxRests = 16;

#define GHOSTMOSAIC_TAP_CELL(T) static_assert (sizeof (T) == 32 && std::is_trivially_copyable<T>::value && std::is_standard_layout<T>::value, #T)
GHOSTMOSAIC_TAP_CELL (Cell); GHOSTMOSAIC_TAP_CELL (Match); GHOSTMOSAIC_TAP_CELL (Used); GHOSTMOSAIC_TAP_CELL (End); GHOSTMOSAIC_TAP_CELL (Reset);
GHOSTMOSAIC_TAP_CELL (Meter); GHOSTMOSAIC_TAP_CELL (Shape); GHOSTMOSAIC_TAP_CELL (Clock); GHOSTMOSAIC_TAP_CELL (ParamsA); GHOSTMOSAIC_TAP_CELL (ParamsB);
GHOSTMOSAIC_TAP_CELL (Play); GHOSTMOSAIC_TAP_CELL (World); GHOSTMOSAIC_TAP_CELL (Gap); GHOSTMOSAIC_TAP_CELL (Rest);
#undef GHOSTMOSAIC_TAP_CELL
static_assert (std::atomic<uint64_t>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free, "the tap's atomics must be lock-free");

// ------------------------------------------------------------------------------------ the queue
// On juce::AbstractFifo (JUCE 8.0.4: single reader, single writer, lock-free), as the engine's other queues.
// A record is written whole or not at all; a refused record is counted, and the next accepted one is
// preceded by a Gap cell with the exact number of lost cells, so the reader knows where the hole is.
class Queue
{
public:
    explicit Queue (int capPow2) : fifo (capPow2), cells (new Cell[(size_t) capPow2]) {}
    bool push (const void* rec, int n, int reserve = 0) noexcept                   // AUDIO only. All n cells or none
    {
        const int gap = pendingLost != 0 ? 1 : 0;
        if (fifo.getFreeSpace() < n + gap + reserve) { pendingLost += (uint32_t) n; lost.fetch_add ((uint64_t) n, std::memory_order_relaxed); return false; }
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n + gap, s1, n1, s2, n2);
        auto slot = [&] (int k) { return &cells[(size_t) (k < n1 ? s1 + k : s2 + k - n1)]; };
        if (gap != 0) { Gap g {}; g.kind = kGap; g.lostCells = pendingLost; std::memcpy (slot (0), &g, 32); pendingLost = 0; }
        for (int i = 0; i < n; ++i) std::memcpy (slot (gap + i), static_cast<const char*> (rec) + 32 * i, 32);
        fifo.finishedWrite (n + gap); return true;
    }
    int read (Cell* out, int max) noexcept                                         // the reader only
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (max, s1, n1, s2, n2);
        if (n1 > 0) std::memcpy (out, &cells[(size_t) s1], (size_t) n1 * 32);
        if (n2 > 0) std::memcpy (out + n1, &cells[(size_t) s2], (size_t) n2 * 32);
        fifo.finishedRead (n1 + n2); return n1 + n2;
    }
    uint64_t lostCells() const noexcept { return lost.load (std::memory_order_relaxed); }
    int capacity() const noexcept { return fifo.getTotalSize(); }

private:
    juce::AbstractFifo fifo; std::unique_ptr<Cell[]> cells;
    uint32_t pendingLost = 0;                               // AUDIO only
    std::atomic<uint64_t> lost { 0 };
};

// ------------------------------------------------------------------------------------ the engine's end of the tap
class Tap
{
public:
    static constexpr int kCapacity = 32768;                // cells: 1 MiB per instance
    static constexpr int kReserve = 64;                    // picks and ends leave this much room for the rare state cells
    Tap() : queue (kCapacity) { for (auto& w : audio.weights) w = 1.0f; }

    // ---- the reader (message thread). One reader at a time: attach() says no to a second one.
    bool attach (const void* owner) noexcept
    {
        const void* none = nullptr;
        if (! m_owner.compare_exchange_strong (none, owner)) return none == owner;
        Cell skip[64];
        while (queue.read (skip, 64) > 0) {}               // what an earlier reader left behind is not ours
        m_resync.fetch_add (1, std::memory_order_relaxed); // the audio thread sends every state cell again
        m_on.store (true, std::memory_order_release);
        return true;
    }
    void detach (const void* owner) noexcept
    {
        if (m_owner.load() != owner) return;
        m_on.store (false, std::memory_order_release);
        m_owner.store (nullptr);
    }
    bool attached() const noexcept { return m_on.load (std::memory_order_relaxed); }
    int read (Cell* out, int max) noexcept { return queue.read (out, max); }
    uint64_t lostCells() const noexcept { return queue.lostCells(); }
    // stage 2 switches (off until the reader asks): the engine's own "used" counters, a slice per buffer;
    // the matched live-input piece's measurements, per pick
    void wantUsage (bool on) noexcept { m_wantUsage.store (on, std::memory_order_relaxed); }
    void wantLive (bool on) noexcept { m_wantLive.store (on, std::memory_order_relaxed); }

    // ---- AUDIO thread only (Engine, Tap.cpp). Plain memory: nothing else reads or writes it.
    struct Audio
    {
        bool on = false, wantUsage = false, wantLive = false;         // latched once per buffer from the atomics below
        uint32_t resyncSeen = 0;
        uint32_t brainGen = 0, targetGen = 0;                         // of the library / target in use (set where they are installed)
        ParamsA pa {}; ParamsB pb {}; Play tr {}; World wo {};   // as last sent (kind 0 = never)
        bool weightsSent = false; float weights[kBands];
        uint32_t soundSig = 0;                                        // hash of the sound list as last sent
        uint32_t sinceState = 0;                                      // samples since every state cell last went out
        uint32_t clock = 0, parked = 0, every = 368, sinceMeter = 0;  // running sample count; buffers spent parked; samples per meter frame
        uint32_t buffers = 0; float peak = 0, limGain = 1, outMin[2] = { 0, 0 }, outMax[2] = { 0, 0 };
        double ppqSent = 0;
        uint32_t resetSeen[2] = { 0, 0 };
        uint32_t curBefore = 0, synBefore = 0; float nextBefore = 0; bool nextExists = false;
        uint32_t mirror = 0; bool mirrorKnown = false;                // (without the private read) the piece the last pick returned
        uint32_t usageAt = 0;                                         // next piece of the "used" sweep
        int lastNote = -1, dryIdx = -1;
        uint32_t rests[kMaxRests]; int numRests = 0; bool restsLost = false;   // pieces rested (Engine::restBlocks) since the last buffer
        Cell scratch[1 + kSoundCells];                                // where a bulk record is put together
    } audio;
    bool on() const noexcept { return m_on.load (std::memory_order_relaxed); }
    uint32_t resyncAsked() const noexcept { return m_resync.load (std::memory_order_relaxed); }
    bool usageWanted() const noexcept { return m_wantUsage.load (std::memory_order_relaxed); }
    bool liveWanted() const noexcept { return m_wantLive.load (std::memory_order_relaxed); }
    Queue queue;

private:
    std::atomic<bool> m_on { false }, m_wantUsage { false }, m_wantLive { false };
    std::atomic<uint32_t> m_resync { 0 };
    std::atomic<const void*> m_owner { nullptr };
};

// ------------------------------------------------------------------------------------ snapshots
// Made once by the worker, never changed afterwards. Rows are flat arrays indexed by piece number.
// Measurements are the engine's own numbers; values that are not finite (digital silence makes NaN in the
// flattened measurements) are stored as 0 and the piece's `flags` say so.
namespace rowFlag { enum : uint8_t { badBands = 1, badTimbre = 2, badFlatBands = 4, badFlatTimbre = 8, silent = 16 }; }
inline constexpr int kEnvSlices = 32;                       // min / max pairs per piece, as int8 fractions of the piece's peak

struct Rows                                                 // per piece
{
    std::vector<float> bands, flatBands;                    // numBlocks * bandCount: energy of each band, as recorded / flattened
    std::vector<float> timbre, flatTimbre;                  // numBlocks * 12
    std::vector<float> freq, peak, rms;                     // the engine's dominant frequency (on a 44100 scale); peak and RMS of the faded piece
    std::vector<int8_t> env;                                // numBlocks * 2 * kEnvSlices (min, max, min, max ...)
    std::vector<uint8_t> flags;                             // rowFlag bits
    size_t bytes() const noexcept
    {
        return (bands.size() + flatBands.size() + timbre.size() + flatTimbre.size() + freq.size() + peak.size() + rms.size()) * sizeof (float)
               + env.size() + flags.size();
    }
};

struct BrainSnapshot
{
    uint32_t gen = 0;                                       // matches World::brainGen
    int numBlocks = 0, blockSize = 0, pcmLen = 0, overlap = 0, hop = 0;
    int windowType = -1;                                    // window::type as built (-1 = not known)
    int bandCount = 0;                                      // min(pcmLen, 100)
    double rate = 0;                                        // the host rate the library was cut at
    bool hasMeasurements = false, hasFlattened = false;     // false = left out to stay inside the byte budget
    struct Sound { juce::String key, name; int first = 0, last = -1; bool enabledAtBuild = true; int64_t lengthSamples = 0; };
    std::vector<Sound> sounds;                              // first .. last inclusive (last < first = no piece); the search never scores `last`
    std::vector<uint16_t> sound;                            // per piece: its sound
    Rows rows;
    std::vector<float> bright, centroidHz;                  // per piece: brightness rank 0..1 and spectral centroid (true Hz)
    std::vector<float> usage0;                              // the engine's "used" counter of each piece at hand-over (raw: not 0 after a file load)
    int synK = 0;                                           // synapses kept per piece (the engine holds up to 1000)
    std::vector<uint16_t> synCount;                         // per piece: how many the engine holds
    std::vector<uint32_t> syn;                              // numBlocks * synK, nearest first, engine order
    std::vector<float> window;                              // pcmLen: the fade as applied to every piece (empty = not known)
    size_t bytes = 0;

    const float* bandsOf (int i) const noexcept      { return rows.bands.data() + (size_t) i * (size_t) bandCount; }
    const float* flatBandsOf (int i) const noexcept  { return rows.flatBands.data() + (size_t) i * (size_t) bandCount; }
    const float* timbreOf (int i) const noexcept     { return rows.timbre.data() + (size_t) i * (size_t) kTimbre; }
    const float* flatTimbreOf (int i) const noexcept { return rows.flatTimbre.data() + (size_t) i * (size_t) kTimbre; }
    const int8_t* envOf (int i) const noexcept       { return rows.env.data() + (size_t) i * (size_t) (2 * kEnvSlices); }
    const uint32_t* synOf (int i) const noexcept     { return syn.data() + (size_t) i * (size_t) synK; }
};

struct TargetSnapshot
{
    uint32_t gen = 0;                                       // matches World::targetGen
    int numBlocks = 0, blockSize = 0, pcmLen = 0, overlap = 0, hop = 0, windowType = -1, bandCount = 0;
    double rate = 0;
    bool hasMeasurements = false, hasFlattened = false;
    juce::String name;
    Rows rows[2];                                           // left, right (a mono target: the same numbers twice)
    std::vector<float> window;
    size_t bytes = 0;

    const float* bandsOf (int ch, int i) const noexcept      { return rows[ch].bands.data() + (size_t) i * (size_t) bandCount; }
    const float* flatBandsOf (int ch, int i) const noexcept  { return rows[ch].flatBands.data() + (size_t) i * (size_t) bandCount; }
    const float* timbreOf (int ch, int i) const noexcept     { return rows[ch].timbre.data() + (size_t) i * (size_t) kTimbre; }
    const float* flatTimbreOf (int ch, int i) const noexcept { return rows[ch].flatTimbre.data() + (size_t) i * (size_t) kTimbre; }
    const int8_t* envOf (int ch, int i) const noexcept       { return rows[ch].env.data() + (size_t) i * (size_t) (2 * kEnvSlices); }
};

// true: this build reads the engine's own private numbers (the normal build). false: the fallback of TapPeek.h, where
// a live target's measurements and the fade shape are not available and the first pick after a restart is not fully explained.
bool fullRead() noexcept;

inline constexpr size_t kSnapshotBudget = (size_t) 192 << 20;   // bytes per snapshot; above it the flattened rows go first, then all measurements
inline constexpr size_t kSynapseBudget = (size_t) 16 << 20;    // bytes of synapses kept per library
}
