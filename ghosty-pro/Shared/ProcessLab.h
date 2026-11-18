#pragma once
// PROCESS LAB: shared offline, seeded, chainable "destructive" processes on a multichannel float buffer.
// Used by Poltergeist SAMPLE LAB and Ghosty Spectrum SPECTRAL LAB (and anything else that holds a sound).
//
// ============================================================ INTEGRATION NOTE ============================================================
// Hosting the panel (ProcessLabPanel.h):
//   1. Implement plab::Host (4 required calls): getBuffer() hands the lab a copy of the sound (plab::Audio = channels of float +
//      sample rate; pass the source's own rate and all its channels), setBuffer(a) receives the committed result (APPLY / BOUNCE /
//      UNDO / REDO / edit commands; the WHOLE file each time, its length may differ from the original), sampleRate() is the host's rate
//      (used when there is no sound yet), audition(clip, loop) plays a preview clip (nullptr = stop).
//      Optional (default implementations exist): tempoBpm() (ruler bars, snap-to-grid, STRETCH TO BARS), slices() (PER SLICE mode),
//      bounceToNew(a) (BOUNCE->NEW keeps the original and hands the result over as a new sound; return false if unsupported).
//      For audition, drop a plab::AuditionPlayer into your processor: play()/stop() from the message thread, render() from processBlock
//      (lock-free, no allocation, fades, seamless crossfaded loops, Hermite resampling + anti-alias filter when the clip's rate differs).
//   2. addAndMakeVisible (new ProcessLabPanel (host)); call panel.reload() whenever the host's sound changes behind the lab's back.
//      The Host must outlive the panel. The panel sizes itself to anything >= 640 x 360 (two-row button bar when narrow).
//   3. State: ProcessLabPanel::getState() / setState() give a juce::ValueTree "PLAB" (chain of ops + params + envelopes + seed +
//      undo budget + view options) that you add to your plugin state. The audio itself stays the host's; the lab never stores it.
//      The same chain tree is Chain::toValueTree()/fromValueTree(); ops are keyed by their stable string id (OpInfo::id) so adding new ops
//      never breaks old states; unknown ids are skipped; hostile values are clamped (64 ops, 256 envelope points, finite params).
// Threading:
//   * plab::Lab owns one persistent worker thread. preview()/apply()/bounce() copy the chain + the region (plus a context margin around
//     it, never the whole file) and process on the worker; results come back on the message thread through Lab::onChanged (always called
//     from the message thread). Progress is Lab::progress() (0..1). cancel() never blocks: it flags the job, which stops within one block
//     (<= 64k samples), frame or grain; a cancelled job leaves the sound untouched. Results of superseded jobs (chain/region/seed edited
//     meanwhile) are rejected (Lab::chainGeneration()).
//   * Everything else (Lab::audio, chain, region, undo) is message-thread-only.
//   * applyOp / applyChain / renderChain are plain functions: call them from any thread you own (tests, batch bounces).
// Determinism: every random choice comes from an xorshift generator seeded by hash (chain seed, op index) (also inside the stretcher),
//   so the same chain + seed + input renders bit-identically on every run OF THE SAME BUILD ON THE SAME PLATFORM. Across platforms results
//   may differ in the last bits (FFT backend: Accelerate on macOS vs JUCE's fallback elsewhere; libm; FMA; the standard library's
//   uniform_real_distribution inside the stretcher). Hosts therefore store RENDERED AUDIO in their projects and never re-render from state.
// Safety: every op's output is capped at kMaxOutSeconds (and 2^26 samples); Iterate's pitch walk reflects at +/-24 st; out-of-memory
//   in a job is caught and reported. Region edges: where the result meets the rest of the file a short adaptive crossfade
//   (Chain::edgeMs, default 3 ms; equal-power for uncorrelated, linear for correlated material) is applied ONLY if the joint would
//   click, so sample-exact edits (e.g. reversing a region between zero crossings) stay bit-exact. Spectral ops and the stretcher see the
//   real audio around the region (handles), not zero padding.
// Undo: UndoStack stores only the pre-op region (not the whole file) plus the length the op produced; undo splices it back bit-exact.
//   Total bytes are capped (default 192 MB, Lab::undo.setBudget); the oldest steps are dropped first, the newest is always kept.
// Dependencies: juce_core, juce_events, juce_audio_basics, juce_dsp, juce_data_structures; Signalsmith Stretch (vendored, MIT;
//   stretch / shift / varispeed-free pitch), so link signalsmith-stretch. ProcessLab.cpp has its own offline multichannel STFT
//   (mono runs once, any frame size, handles); GhostyPro/fx/SpectralCore.h is used for its helpers.
//   ProcessLabPanel.cpp adds juce_gui_basics + Shared/GhostArt.h (colours / font). Build: add Shared/ProcessLab.cpp and
//   Shared/ProcessLabPanel.cpp to the plugin target (Shared on the include path). Tests: -DGHOSTY_BUILD_PROCESSLAB_TESTS=ON.
// ==========================================================================================================================================
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <deque>
#include <array>
#include <atomic>
#include <memory>
#include <functional>

namespace plab {

constexpr double kMaxOutSeconds = 600.0;          // no op ever renders more than this (nor more than 2^26 samples per channel)
int64_t maxOutSamples (double rate);
int contextSamples (double rate);                  // audio kept around a region for spectral/stretch handles + edge blends

// ---------------------------------------------------------------- audio + region
struct Audio {
    std::vector<std::vector<float>> ch;      // ch[channel][sample]
    double rate = 48000.0;
    int channels() const { return (int) ch.size(); }
    int length() const { return ch.empty() ? 0 : (int) ch[0].size(); }
    bool empty() const { return length() == 0; }
    void resize (int numChannels, int numSamples) { ch.assign ((size_t) juce::jmax (0, numChannels), std::vector<float> ((size_t) juce::jmax (0, numSamples), 0.0f)); }
    size_t bytes() const { size_t b = 0; for (auto& c : ch) b += c.size() * sizeof (float); return b; }
    static Audio fromBuffer (const juce::AudioBuffer<float>& b, double rate);
    juce::AudioBuffer<float> toBuffer() const;
    Audio slice (int start, int length) const;                 // a copy of [start, start + length) (clipped)
    void splice (int start, int length, const Audio& with);    // replace [start, start + length) by `with` (any length)
    float peak() const;
    float rms() const;
};

struct Region {
    int start = 0, length = 0;
    int end() const { return start + length; }
    bool empty() const { return length <= 0; }
    static Region all (const Audio& a) { return { 0, a.length() }; }
    Region clipped (const Audio& a) const;
    bool operator== (const Region& o) const { return start == o.start && length == o.length; }
    bool operator!= (const Region& o) const { return ! (*this == o); }
};

// ---------------------------------------------------------------- deterministic random
struct Rng {
    uint32_t s = 1;
    explicit Rng (uint32_t seed = 1) { s = seed ? seed : 0x9e3779b9u; next(); next(); }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bi() { return uni() * 2.0f - 1.0f; }                             // [-1, 1)
    int below (int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
    // UniformRandomBitGenerator (used as the stretcher's engine)
    using result_type = uint32_t;
    static constexpr result_type min() { return 1u; }
    static constexpr result_type max() { return 0xffffffffu; }
    result_type operator()() { return next(); }
};
uint32_t mixSeed (uint32_t seed, uint32_t index);   // the op's own seed inside a chain

// ---------------------------------------------------------------- ops
enum class OpType : int {
    CycleShuffle = 0, CycleRepeat, CycleOmit, CycleReverse, CycleInterleave,
    Brassage, Iterate,
    SpecBlur, SpecTrace, SpecFreeze, SpecStretch, SpecShift,
    Envelope, Extend, Normalise, Reverse, FadeIn, FadeOut,
    // added 2026-10-22 (final gate); ids are stable strings, new ops only ever go before Count
    Gain, Invert, DcRemove, Stereo, Varispeed, Drive, Bitcrush, FilterSweep, FreqShift,
    SpecGate, SpecBinShift, CrossSynth, SpecExtreme, StretchTo,
    CycleAverage, CycleTranspose, CycleDistort,
    Count
};
constexpr int kMaxParams = 10;
enum ParamKind : int { Continuous = 0, Toggle = 1, Choice = 2 };
struct EnvPoint { float x = 0, y = 0, curve = 0; };   // breakpoint; curve (-1..1) bends the segment to the next point
constexpr int kMaxEnvPoints = 256;
constexpr int kMaxChainOps = 64;
struct ParamInfo { const char* name; const char* unit; float min, max, def; bool integer; int kind = Continuous; const char* choices = nullptr; };   // choices "a|b|c"
struct OpInfo {
    const char* id;        // stable state key
    const char* name;      // UI
    const char* group;     // UI menu group
    const char* blurb;     // one line
    int numParams;
    ParamInfo params[kMaxParams];
    bool usesSeed, changesLength, hasEnvelope;
    bool usesSide = false;     // CrossSynth: reads Op::side (B sound)
    bool usesTempo = false;    // StretchTo: bpm param defaults to the host tempo
};
const OpInfo& info (OpType t);
const OpInfo* infoById (const juce::String& id);   // nullptr when unknown
juce::StringArray choiceNames (const ParamInfo& p);

struct Op {
    OpType type = OpType::Normalise;
    std::array<float, kMaxParams> p {};
    std::vector<EnvPoint> env;                  // Envelope op: x 0..1 (region time), y 0..1 (gain), sorted by x
    bool enabled = true;
    std::shared_ptr<const Audio> side;          // runtime only (not saved): CrossSynth's B sound (the Lab fills it: clipboard or the audio after the region)
    Op() { setDefaults(); }
    explicit Op (OpType t) : type (t) { setDefaults(); }
    void setDefaults();                         // params + a default envelope from info()
    void sanitise();                            // clamp params to their ranges, sort/clamp/cap the envelope
    juce::ValueTree toValueTree() const;
    static bool fromValueTree (const juce::ValueTree& v, Op& out);   // false = unknown op id
};

struct Chain {
    std::vector<Op> ops;
    uint32_t seed = 1;
    float edgeMs = 3.0f;                        // adaptive edge crossfade where the result meets the file (0 = off)
    juce::ValueTree toValueTree() const;        // type "PLCHAIN"
    static Chain fromValueTree (const juce::ValueTree& v);
    juce::String toXml() const;
    static Chain fromXml (const juce::String& xml);
    bool changesLength() const;
};

// ---------------------------------------------------------------- progress / cancel (shared between threads)
struct Progress {
    std::atomic<float> fraction { 0.0f };
    std::atomic<bool> cancel { false };
    bool tick (float f) { fraction.store (juce::jlimit (0.0f, 1.0f, f)); return ! cancel.load(); }   // false = stop now
    bool alive() const { return ! cancel.load(); }
    void reset() { fraction.store (0.0f); cancel.store (false); note.clear(); }
    juce::String note;    // written by the rendering thread (e.g. "output capped at 600 s"); read once the job has finished
};

// ---------------------------------------------------------------- the processes
// Render one op on a slice (the region's samples alone, no context). The result may have another length. Returns false when cancelled
// (out is then unspecified). Pure: same slice + op + seed -> same output.
bool renderOp (const Audio& slice, const Op& op, uint32_t seed, Audio& out, Progress* prog = nullptr);
// Apply one op to region r of a (in place, with context handles + edge blends); r becomes the region the result occupies.
bool applyOp (Audio& a, Region& r, const Op& op, uint32_t seed, Progress* prog = nullptr);
// Render the whole chain on region r of src: out = the processed region alone (it uses the audio around r as context and blends the
// edges the way applyChain would splice them). Cancelled -> false.
bool renderChain (const Audio& src, Region r, const Chain& c, Audio& out, Progress* prog = nullptr);
// Apply the whole chain to region r of a (in place, all or nothing); r becomes the result's region. Cancelled -> false, a untouched.
bool applyChain (Audio& a, Region& r, const Chain& c, Progress* prog = nullptr);

// wavecycle detection (shared with tests): rising zero crossings of the DC-blocked, low-passed (zero-phase) mono mix with hysteresis,
// at least minLenMs apart, grouped `group` cycles per unit. Returns unit boundaries b0 < b1 < ...; samples before b0 / after the last stay.
std::vector<int> cycleUnits (const Audio& slice, int group);
std::vector<int> cycleUnits (const Audio& slice, int group, float minLenMs);

// ---------------------------------------------------------------- undo
class UndoStack {
public:
    explicit UndoStack (size_t budgetBytes = (size_t) 192 << 20) : budget (budgetBytes) {}
    struct Capture { int start = 0; std::vector<std::vector<float>> samples; juce::String name; };
    static Capture capture (const Audio& a, Region before, const juce::String& name);  // call before an edit
    void push (Capture&& c, Region after);                                              // call after it (after = where the result sits)
    bool undo (Audio& a, Region& restored);   // bit-exact: the after-region is replaced by the captured samples
    bool redo (Audio& a, Region& restored);
    bool canUndo() const { return ! past.empty(); }
    bool canRedo() const { return ! future.empty(); }
    juce::String undoName() const { return past.empty() ? juce::String() : past.back().name; }
    juce::String redoName() const { return future.empty() ? juce::String() : future.back().name; }
    int depth() const { return (int) past.size(); }
    size_t bytes() const;
    void setBudget (size_t b) { budget = b; trim(); }
    size_t getBudget() const { return budget; }
    void clear() { past.clear(); future.clear(); }
private:
    struct Entry { juce::String name; int start = 0; int otherLength = 0; std::vector<std::vector<float>> samples; size_t bytes() const; };
    static void swapIn (Audio& a, Entry& e);
    void trim();
    std::deque<Entry> past, future;
    size_t budget;
};

// ---------------------------------------------------------------- session: sound + chain + region + undo + worker
class Lab : private juce::Thread {
public:
    Lab();
    ~Lab() override;

    // ---- message thread state
    Audio audio;
    Chain chain;
    Region region;                    // the selection the chain works on
    UndoStack undo;
    std::function<void()> onChanged;  // message thread: anything changed (sound, preview, progress, busy)
    std::function<void (const juce::String&)> onMessage;   // message thread: a one-line status / error

    void setAudio (Audio a);          // new sound: clears undo + preview, selects all
    void setRegion (Region r);        // clipped; invalidates the preview
    void chainChanged();              // call after editing chain (invalidates the preview, cancels a running preview)

    // ---- jobs (message thread; a running job is cancelled first; never blocks)
    void preview();                   // render chain on region -> previewAudio (nothing committed)
    void apply();                     // commit (reuses a valid preview at once, else renders) as one undo step (chain kept)
    void bounce();                    // apply, then clear the chain and select the result (ready to process again)
    void cancel();
    bool busy() const { return running.load(); }
    float progress() const { auto p = curProg; return p ? p->fraction.load() : 0.0f; }
    bool undoStep();
    bool redoStep();

    // ---- preview result (message thread)
    std::shared_ptr<const Audio> previewAudio;   // the processed region alone, or nullptr
    Region previewRegion;                        // where it would land (start = region.start)
    bool previewValid() const { return previewAudio != nullptr && previewFor == region && previewGen == chainGen && ! previewDirty; }
    void invalidatePreview() { previewDirty = true; ++chainGen; }
    std::shared_ptr<const Audio> regionAudio() const;   // a copy of the selected region (for auditioning the source)

    // ---- committed-result hook (message thread, after apply/bounce/undo/redo/edits changed `audio`)
    std::function<void (const Audio&)> onCommitted;

    // ---- additions (final gate)
    uint32_t chainGeneration() const { return chainGen; }    // bumped by every chain / seed / region edit
    void repeatLast();                                        // run the last applied chain again on the current region
    bool hasLast() const { return ! lastChain.ops.empty(); }
    void applyPerSlice (std::vector<Region> slices);          // the chain on every slice separately, one undo step
    double tempoBpm = 0.0;                                    // host tempo (0 = unknown): STRETCH TO default
    // edit commands (immediate, one undo step each)
    bool crop();                      // keep only the region
    bool deleteRegion();
    bool silenceRegion();
    bool insertSilence (double seconds);   // at region start
    bool copyRegion();
    bool cutRegion();
    bool paste();                     // insert the clipboard at region start
    bool mixPaste (float gain = 1.0f);// mix the clipboard into the file from region start
    bool findLoop();                  // move the region end (+/-5 %) to the best loop point (zero crossing, best match with the start)
    std::shared_ptr<const Audio> clipboard;
    juce::String lastNote;            // the last job's note (caps, no wavecycles found, ...)
    size_t lastJobBytes() const { return jobBytes; }   // audio copied into the last job (region + context only)

private:
    enum class Kind { Preview, Apply, Bounce, PerSlice };
    struct Job {
        Kind kind; Audio src; Region r; Chain c; uint32_t gen = 0, cgen = 0; int offset = 0;   // src = region + context; offset = src start in the file
        std::vector<Region> slices; std::shared_ptr<Progress> prog;
    };
    void start (Kind k);
    void run() override;
    void finished (std::unique_ptr<Job> job, bool ok, std::shared_ptr<Audio> result, juce::String note);
    void commit (const Audio& result, Region src, const Chain& used, Kind k, uint32_t cgen);
    bool edit (Region before, const Audio& with, const juce::String& name, Region select);
    std::shared_ptr<const Audio> sideFor (Region r) const;
    std::unique_ptr<Job> pending;
    std::shared_ptr<Progress> curProg, workerProg;
    std::atomic<bool> running { false };
    uint32_t jobGen = 0, chainGen = 1, previewGen = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    Region previewFor; bool previewDirty = false;
    Chain previewChain, lastChain;
    Kind curKind = Kind::Preview;
    size_t jobBytes = 0;
    juce::CriticalSection jobLock;
};

// ---------------------------------------------------------------- audition player (real-time safe)
// Message thread: play()/stop(). Audio thread: render() adds the clip into the block; no allocation, no locks, 5 ms fades on
// start / stop, equal-power crossfaded loop joins (no gap), Hermite resampling + a 2-pole anti-alias low-pass when the clip's rate is
// higher than the host's. The clip's shared_ptr is only released on the message thread (inside play()/stop()/release()).
class AuditionPlayer {
public:
    void prepare (double hostRate) { rate.store (hostRate > 0 ? hostRate : 48000.0); }
    void play (std::shared_ptr<const Audio> clip, bool loop);
    void stop();                           // fades out, then silence
    void release();                        // message thread, when idle: drop held clips (e.g. before destruction)
    bool playing() const { return state.load() != Idle; }
    float position() const { return pos01.load(); }   // 0..1 playhead inside the clip
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples, float gain = 1.0f);
private:
    enum : int { Idle = 0, Playing = 1, Stopping = 2 };
    std::shared_ptr<const Audio> slots[2];
    std::atomic<int> active { -1 }, inUse { -1 }, state { Idle };
    std::atomic<bool> loopFlag { false };
    std::atomic<double> rate { 48000.0 };
    std::atomic<float> pos01 { 0.0f };
    std::atomic<bool> writing { false };      // message thread is replacing a slot: the audio thread skips this block
    std::atomic<uint32_t> playGen { 0 };      // bumped by every play(): the audio thread restarts from 0
    double readPos = 0.0; float fade = 0.0f; uint32_t startedGen = 0;
    std::array<float, 8> last {}, tail {}; float tailGain = 0.0f;   // de-click when a new clip replaces a playing one
    std::array<bool, 2> slotLinear {};   // loop join: linear crossfade for correlated tail/head (e.g. DC, steady tones), else equal power
};

// ---------------------------------------------------------------- helpers (shared with tests / hosts)
void normaliseTo (Audio& a, float peakDb);
Audio monoMix (const Audio& a);
float maxAbsDiff (const std::vector<float>& x, int order);   // max |n-th difference| (1 = first, 2 = second)
int fftOrderFor (int order48k, double rate);                 // FFT size scaled with the sample rate (same Hz resolution everywhere)

} // namespace plab
