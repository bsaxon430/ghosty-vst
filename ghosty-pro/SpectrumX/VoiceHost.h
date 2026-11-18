#pragma once
// Ghosty Spectrum VOICE host: the two source slots (A = the sound / carrier, B = the formant source), the render
// settings, undo / redo, a worker thread that analyses and renders (progress + time left + cancel, newest request
// wins), the rendered result (mono or stereo), WAV export for drag-out (renders first when out of date), a pitch-set
// batch render, state save / load, and a preview player for the audio thread (A/B compare, loop, click-free).
// Self-contained: the Spectrum processor owns one (see the INTEGRATION NOTE in VoicePage.h).
#include "VoiceEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <deque>
#include <memory>
#include <mutex>

namespace voice {

// The audio of a slot (shared by undo snapshots; never changes after creation)
struct Pcm {
    std::vector<std::vector<float>> ch;              // the two channels of a stereo sound (empty for mono)
    std::vector<float> mono;                         // what is analysed (the mix of the channels / the mono sound)
    int channels() const { return ch.empty() ? 1 : 2; }
    const float* channel(int c) const { return ch.empty() ? mono.data() : ch[(size_t) juce::jlimit(0, 1, c)].data(); }
    int fs = 0, bits = 24; juce::String name; float rms = 0;
    int length() const { return (int) mono.size(); }
    double seconds() const { return fs > 0 ? mono.size() / (double) fs : 0.0; }
    size_t bytes() const { size_t b = mono.capacity() * sizeof(float); for (auto& c : ch) b += c.capacity() * sizeof(float); return b; }
    AudioRef ref() const { AudioRef r; if (ch.empty()) r.ch.push_back(mono.data()); else for (auto& c : ch) r.ch.push_back(c.data()); r.n = length(); r.fs = fs; return r; }
};
using PcmPtr = std::shared_ptr<const Pcm>;

// A loaded sound: its audio + (once analysed) its analysis. The worker publishes the analysis once; readers take a
// shared_ptr copy (analysis()) so a dropped / replaced analysis is never freed under them.
struct Source {
    PcmPtr pcm; AnalysisOptions opts; juce::uint32 id = 0;
    std::shared_ptr<const Analysis> analysis() const { return std::atomic_load(&an); }
    bool ready() const { return analysis() != nullptr; }
    void publish(std::shared_ptr<const Analysis> a) const { std::atomic_store(&an, std::move(a)); }
    void drop() const { std::atomic_store(&an, std::shared_ptr<const Analysis>()); }
    mutable std::atomic<bool> queued { false }, failed { false }, cancel { false };
    mutable std::mutex blobLock; mutable juce::String blob;     // FLAC + base64 cache for the state
    double seconds() const { return pcm ? pcm->seconds() : 0.0; }
    const juce::String& name() const { static const juce::String none; return pcm ? pcm->name : none; }
    int fs() const { return pcm ? pcm->fs : 0; }
private:
    mutable std::shared_ptr<const Analysis> an;
};
using SourcePtr = std::shared_ptr<const Source>;

struct Result {
    Rendered audio; RenderInfo info; RenderParams params; juce::uint32 serial = 0;
    int paramsVersion = -1, sourceVersion = -1;      // what it was rendered from (stale when these move on)
    float rms = 0;
    int fs() const { return audio.fs; }
    double seconds() const { return audio.fs > 0 ? audio.L.size() / (double) audio.fs : 0.0; }
};
using ResultPtr = std::shared_ptr<const Result>;

// single-producer (under a lock) / single-consumer (audio thread) hand-off; the audio thread never frees
template <typename T> class Handoff {
public:
    ~Handoff() { delete pending.load(); delete current; delete retired.load(); }
    void publish(std::unique_ptr<T> p) { collect(); delete pending.exchange(p.release()); }
    T* get() { if (retired.load(std::memory_order_acquire) == nullptr) if (T* p = pending.exchange(nullptr)) { retired.store(current, std::memory_order_release); current = p; } return current; }
    void collect() { delete retired.exchange(nullptr); }
private:
    std::atomic<T*> pending { nullptr }, retired { nullptr }; T* current = nullptr;
};

enum Slot { SlotA = 0, SlotB = 1 };
enum PlayWhat { PlayNone = 0, PlayResult = 1, PlayA = 2, PlayB = 3 };

class VoiceHost : private juce::Thread {
public:
    static constexpr double kMaxSeconds = 300.0;            // 5 minutes per slot (longer files are trimmed, with a banner)
    static constexpr size_t kUndoAudioBudget = 768u << 20;  // audio kept alive by the undo history
    static constexpr int kAnalysisCache = 2;                // analyses kept for sources that are not in a slot
    VoiceHost();
    ~VoiceHost() override;

    // ---- sources
    bool loadFile(int slot, const juce::File& f, juce::String& err);                 // decode + analyse now (tests)
    void loadFileAsync(int slot, const juce::File& f);                               // decode + analyse on the worker
    bool setAudio(int slot, std::vector<std::vector<float>> channels, int fs, const juce::String& name, bool async, juce::String* err = nullptr, int bits = 24);
    bool setAudio(int slot, std::vector<float> mono, int fs, const juce::String& name, bool async, juce::String* err = nullptr) {
        std::vector<std::vector<float>> c; c.push_back(std::move(mono)); return setAudio(slot, std::move(c), fs, name, async, err); }
    void clearSlot(int slot, bool undoable = true);
    void swapSlots();
    bool useResultAs(int slot, bool async = true);                                   // RESULT -> A / B (bounce, undoable)
    SourcePtr source(int slot) const;
    std::shared_ptr<const Analysis> analysis(int slot) const { auto s = source(slot); return s ? s->analysis() : nullptr; }
    static bool decodeFile(const juce::File& f, std::vector<std::vector<float>>& ch, int& fs, int& bits, juce::String& err, juce::String& note);
    static juce::StringArray audioExtensions();
    juce::String trimmedNote(int slot) const;                                        // "trimmed to 5:00" banner text (empty = whole file)

    // ---- settings + undo. Every change is one undo step; a gesture (drag) or a burst on one control (<400 ms) is one step.
    RenderParams params() const;
    void setParams(const RenderParams& p, bool undoable = true, const juce::String& coalesceKey = {});
    AnalysisOptions analysisOptions() const;
    void setAnalysisOptions(const AnalysisOptions& o, bool undoable = true);        // re-analyses both slots on the worker
    void beginGesture();
    void endGesture();
    bool undo(); bool redo(); bool canUndo() const; bool canRedo() const;
    size_t undoAudioBytes() const;                                                   // tests
    size_t liveAnalysisBytes() const;                                                // analyses reachable from slots / undo (tests)
    std::atomic<bool> autoRender { true };

    // ---- render (worker). The newest request wins: a running render is cancelled and restarted.
    void requestRender();
    void cancel();                                                                   // stops the running job + drops queued ones
    void reanalyse();                                                                // analyse slots that aren't analysed (after a cancel)
    bool renderNow(juce::String& err);                                               // synchronous (tests, drag-out of a stale result)
    bool waitIdle(int timeoutMs);
    void runOnWorker(std::function<void()> task) { post(std::move(task)); }   // guarded like every worker job
    ResultPtr result() const;
    bool resultStale() const;                                                        // settings / sources changed since the result
    Curve traceContour(double stepSec = 0.03) const;                                 // A's detected pitch as an editable curve (source time)
    // RENDER SET: one file per pitch (semitones relative to the current settings) into a new folder; async
    void renderSet(std::vector<int> semis, bool toSharedFolder);
    juce::File lastSetFolder() const;
    static juce::File sharedSoundsDir();

    // ---- status for the UI
    std::atomic<int> progress { -1 };                // 0..100 while a job runs, -1 idle
    std::atomic<int> job { 0 };                      // 0 idle, 1 analysing, 2 rendering, 3 render set, 4 decoding
    std::atomic<int> resultVersion { 0 }, sourceVersion { 0 }, paramsVersion { 0 };
    double secondsLeft() const;                      // estimate for the running job (-1 unknown)
    juce::String status() const;                     // the latest message (cleared on source / mode changes)
    juce::uint32 statusTime() const;

    // ---- drag-out / export
    juce::File writeResultWav(juce::String& why, bool renderIfStale = true);        // 24-bit WAV in the temp folder
    static juce::File exportDir();
    static void cleanExportDir(double maxAgeHours = 24.0);
    static bool writeWav(const juce::File& f, const Rendered& x);
    static bool writeWav(const juce::File& f, const std::vector<float>& x, int fs) { Rendered r; r.L = x; r.fs = fs; return writeWav(f, r); }

    // ---- state
    juce::ValueTree toTree() const;                  // settings + both sources (FLAC 16/24-bit, base64) + UI prefs
    void fromTree(const juce::ValueTree& t);         // re-analyses on the worker
    static juce::ValueTree paramsToTree(const RenderParams& p);
    static RenderParams paramsFromTree(const juce::ValueTree& t);
    static void sanitise(RenderParams& p);
    juce::String uiPrefs() const; void setUiPrefs(const juce::String& s);

    // ---- preview player (process() on the audio thread; the rest on the message thread)
    void prepare(double sampleRate, int maxBlock);
    void process(juce::AudioBuffer<float>& out);     // ADDS the preview to out
    void play(int what, double fromSeconds = 0.0);   // seconds in that sound's own timeline
    void stop() { play(PlayNone); }
    void toggleAB();                                 // result <-> A at the same musical position
    void setLoop(bool on, double srcStart = -1, double srcEnd = -1);   // loop (source seconds; < 0 = the whole sound); saved in the state
    bool loopEnabled() const { return loopOn.load(); }
    std::pair<double, double> loopRange() const;                       // source seconds (-1, -1 = none)
    juce::File writeRangeWav(double srcA, double srcB, juce::String& why);   // the WAV grip: the result (else A) between two source times
    std::atomic<bool> levelMatch { true };           // A / B / RESULT previews matched to the result's loudness
    std::atomic<int> playing { PlayNone };
    std::atomic<double> playPos { -1.0 };            // seconds of what is playing, -1 = stopped
    std::atomic<float> previewGain { 0.85f };

private:
    void run() override;
    struct Snap { RenderParams p; AnalysisOptions o; SourcePtr a, b; };
    Snap snap() const;
    void restore(const Snap& s);
    void pushUndo(bool force = false);
    void trimMemory();                               // under lock
    void post(std::function<void()> task);
    void changed(bool sourcesChanged);
    void publishPlay();
    void enqueueAnalysis(const SourcePtr& s);
    static juce::String encode(const Source& s);
    static bool decodeBlob(const juce::String& b64, std::vector<std::vector<float>>& ch, int& fs, int& bits);
    static std::shared_ptr<Pcm> makePcm(std::vector<std::vector<float>> ch, int fs, const juce::String& name, int bits, juce::String* note);
    std::shared_ptr<Source> makeSource(PcmPtr pcm);
    bool doRender(bool sync, juce::String* err);
    void setStatus(const juce::String& s);
    void guarded(const std::function<void()>& f);    // runs f, turns bad_alloc / exceptions into a status line

    mutable std::mutex lock;                         // params, options, sources, result, status, undo
    RenderParams prm; AnalysisOptions opt; SourcePtr src[2]; ResultPtr res; juce::String stat, ui; juce::uint32 statT = 0;
    juce::String trimNote[2]; juce::File setFolder;
    std::deque<Snap> undoStack, redoStack; bool inGesture = false, gesturePushed = false;
    juce::String lastKey; juce::uint32 lastKeyMs = 0;
    std::deque<juce::uint32> analysedLru;            // ids of recently analysed sources (analysis cache)
    std::deque<std::function<void()>> tasks;
    std::atomic<bool> renderWanted { false }, cancelFlag { false }, busyTask { false };
    std::atomic<int> cancelGen { 0 }, contentVersion { 0 };
    std::atomic<juce::uint32> serial { 0 }, nextId { 1 };
    std::atomic<double> jobStart { 0 };
    juce::WaitableEvent wake;

    // audio thread
    struct PlayItem { ResultPtr r; PcmPtr a, b; float gainA = 1, gainB = 1, gainR = 1; };
    Handoff<PlayItem> items; std::mutex publishLock;
    double hostRate = 48000.0, pos = 0; int cur = PlayNone; float fade = 0; int pendingWhat = PlayNone; double pendingFrom = 0; bool switching = false;
    std::atomic<int> playReq { 0 }; int playSeen = 0; std::atomic<double> playFrom { 0.0 }; std::atomic<int> playWhatReq { PlayNone };
    std::atomic<bool> loopOn { false }; std::atomic<double> loopS0 { -1 }, loopS1 { -1 };   // in the playing sound's own seconds
    double loopSrc0 = -1, loopSrc1 = -1; mutable std::mutex loopLock;   // the loop in source seconds
    void applyLoopFor(int what);
};
} // namespace voice
