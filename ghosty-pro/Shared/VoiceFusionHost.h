#pragma once
// GHOST VOICE host (shared: Ghosty Spectrum's GHOST VOICE page now, a standalone Ghost Voice plugin later): the three slots
// (A = your performance, B / C = the voices blended in: a recording or a designed identity), the settings + undo / redo, a worker
// thread that analyses and renders (progress, cancel, newest request wins), the recorder (count-in clicks on the audio thread,
// the take lands in a slot), the result, a preview player for the audio thread (result / dry A / B / C, A/B compare, click-free),
// WAV export (drag-out / save, always through the caller's demo gate), voice presets (Documents/Ghosty/Ghosty Spectrum/Voices)
// and the state. Self-contained: the processor owns one; the audio thread only calls prepare / process / processRecord.
#include "VoiceFusion.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <deque>
#include <memory>
#include <mutex>

namespace fusion {

struct Take {                                    // the audio of a slot (shared by undo snapshots; never changes after creation)
    std::vector<float> mono; int fs = 0; juce::String name; float rms = 0;
    int length() const { return (int) mono.size(); }
    double seconds() const { return fs > 0 ? mono.size() / (double) fs : 0.0; }
    voice::AudioRef ref() const { voice::AudioRef r; r.ch.push_back(mono.data()); r.n = length(); r.fs = fs; return r; }
};
using TakePtr = std::shared_ptr<const Take>;

// a slot: a take (analysed on the worker) and / or an identity (extracted from the take, or a designed / saved voice)
struct Slot {
    TakePtr take; juce::uint32 id = 0; juce::String presetName;   // presetName set = the identity came from a preset (no take)
    std::shared_ptr<const voice::Analysis> analysis() const { return std::atomic_load(&an); }
    std::shared_ptr<const Identity> identity() const { return std::atomic_load(&idn); }
    bool ready() const { return take ? analysis() != nullptr : identity() != nullptr; }
    void publish(std::shared_ptr<const voice::Analysis> a, std::shared_ptr<const Identity> i) const { std::atomic_store(&an, std::move(a)); std::atomic_store(&idn, std::move(i)); }
    mutable std::atomic<bool> queued { false }, failed { false }, cancel { false };
    mutable std::mutex blobLock; mutable juce::String blob;      // FLAC + base64 of the take (state cache)
    double seconds() const { return take ? take->seconds() : 0.0; }
    juce::String name() const { return take ? take->name : presetName; }
    bool empty() const { return !take && !identity(); }
private:
    mutable std::shared_ptr<const voice::Analysis> an; mutable std::shared_ptr<const Identity> idn;
};
using SlotPtr = std::shared_ptr<const Slot>;

struct Result { voice::Rendered audio; Info info; Params params; juce::uint32 serial = 0; int paramsVersion = -1, sourceVersion = -1; float rms = 0;
    double seconds() const { return audio.fs > 0 ? audio.L.size() / (double) audio.fs : 0.0; } };
using ResultPtr = std::shared_ptr<const Result>;

struct Preset { juce::String name; Identity identity; Params params; juce::ValueTree toTree() const; static Preset fromTree(const juce::ValueTree&); };

enum SlotIx { SlotA = 0, SlotB = 1, SlotC = 2 };
enum PlayWhat { PlayNone = 0, PlayResult = 1, PlayA = 2, PlayB = 3, PlayC = 4 };

template <typename T> class Handoff {            // single producer (under a lock) / single consumer (audio thread); the audio thread never frees
public:
    ~Handoff() { delete pending.load(); delete current; delete retired.load(); }
    void publish(std::unique_ptr<T> p) { collect(); delete pending.exchange(p.release()); }
    T* get() { if (retired.load(std::memory_order_acquire) == nullptr) if (T* p = pending.exchange(nullptr)) { retired.store(current, std::memory_order_release); current = p; } return current; }
    void collect() { delete retired.exchange(nullptr); }
private:
    std::atomic<T*> pending { nullptr }, retired { nullptr }; T* current = nullptr;
};

class FusionHost : private juce::Thread {
public:
    static constexpr double kMaxSeconds = 180.0;            // 3 minutes per slot
    static constexpr double kMaxRecordSeconds = 120.0;
    FusionHost();
    ~FusionHost() override;

    // ---- slots (message thread)
    bool setTake(int slot, std::vector<float> mono, int fs, const juce::String& name, bool async, juce::String* err = nullptr);
    void loadFileAsync(int slot, const juce::File& f);
    bool loadFile(int slot, const juce::File& f, juce::String& err);          // decode + analyse now (tests)
    static bool decodeFile(const juce::File& f, std::vector<float>& mono, int& fs, juce::String& err);
    static juce::StringArray audioExtensions();
    void setIdentity(int slot, const Identity& id, const juce::String& presetName);   // B / C from a designed / saved voice
    void clearSlot(int slot, bool undoable = true);
    SlotPtr slot(int i) const;
    std::shared_ptr<const voice::Analysis> analysis(int i) const { auto s = slot(i); return s ? s->analysis() : nullptr; }
    std::shared_ptr<const Identity> identity(int i) const { auto s = slot(i); return s ? s->identity() : nullptr; }

    // ---- settings + undo (every change one step; a drag gesture or a burst on one control (< 400 ms) is one step)
    Params params() const;
    void setParams(const Params& p, bool undoable = true, const juce::String& coalesceKey = {});
    void beginGesture(); void endGesture();
    bool undo(); bool redo(); bool canUndo() const; bool canRedo() const;
    std::atomic<bool> autoRender { true };

    // ---- render (worker; the newest request wins)
    void requestRender();
    void cancel();
    void reanalyse();
    bool renderNow(juce::String& err);
    bool waitIdle(int timeoutMs);
    ResultPtr result() const;
    bool resultStale() const;
    std::atomic<int> progress { -1 };                // 0..100 while a job runs, -1 idle
    std::atomic<int> job { 0 };                      // 0 idle, 1 analysing, 2 rendering, 4 decoding
    std::atomic<int> resultVersion { 0 }, sourceVersion { 0 }, paramsVersion { 0 };
    double secondsLeft() const;
    juce::String status() const; juce::uint32 statusTime() const;
    void setStatus(const juce::String& s);

    // ---- recorder (audio thread: processRecord; the rest on the message thread)
    void startRecording(int slot, int countInBeats, double bpm);   // countInBeats 0 = straight away
    void stopRecording();                                         // the take goes to its slot (analysed on the worker)
    bool isRecording() const { return recording.load(); }
    bool isCountingIn() const { return countingIn.load(); }
    int countInLeft() const { return countLeft.load(); }
    int recordSlot() const { return recSlot.load(); }
    double recordedSeconds() const { return recLen.load() / std::max(1.0, hostRate); }
    float inputLevel() const { return inLevel.load(); }
    void pollRecording();                                          // message thread timer: finishes a take the audio thread completed (120 s cap)
    void processRecord(const float* inL, const float* inR, int n, juce::AudioBuffer<float>& out);   // copies the input, adds the count-in clicks to out

    // ---- preview player
    void prepare(double sampleRate, int maxBlock);
    void process(juce::AudioBuffer<float>& out);     // ADDS the preview
    void play(int what, double fromSeconds = 0.0);
    void stop() { play(PlayNone); }
    void toggleAB();                                 // result <-> dry A at the same spot
    std::atomic<int> playing { PlayNone }; std::atomic<double> playPos { -1.0 }; std::atomic<float> previewGain { 0.85f }; std::atomic<bool> levelMatch { true };

    // ---- export (the CALLER asks the demo gate first) + presets
    juce::File writeResultWav(juce::String& why, bool renderIfStale = true);        // 24-bit WAV in the temp folder (drag-out)
    bool exportResultTo(const juce::File& f, juce::String& why);                     // the save dialog's file
    static juce::File exportDir();
    static void cleanExportDir(double maxAgeHours = 24.0);
    static bool writeWav(const juce::File& f, const voice::Rendered& x);
    static juce::File voicesDir();                                                   // Documents/Ghosty/Ghosty Spectrum/Voices
    static juce::StringArray presetFiles();                                          // *.ghostvoice in voicesDir (names)
    bool saveVoice(const juce::String& name, juce::String& err, juce::File* written = nullptr);   // the blended identity + the settings
    bool loadVoice(const juce::File& f, int slot, bool withSettings, juce::String& err);
    Identity currentBlendIdentity() const;                                           // A's identity fused with B / C at the blend

    // ---- state
    juce::ValueTree toTree() const;
    void fromTree(const juce::ValueTree& t);
    juce::String uiPrefs() const; void setUiPrefs(const juce::String& s);

private:
    void run() override;
    struct Snap { Params p; SlotPtr s[3]; };
    Snap snap() const; void restore(const Snap& s); void pushUndo(bool force = false); void trimMemory();
    void post(std::function<void()> task); void changed(bool sources); void publishPlay(); void enqueueAnalysis(const SlotPtr& s, int slotIx);
    static juce::String encode(const Slot& s); static bool decodeBlob(const juce::String& b64, std::vector<float>& mono, int& fs);
    static std::shared_ptr<Take> makeTake(std::vector<float> mono, int fs, const juce::String& name);
    std::shared_ptr<Slot> makeSlot(TakePtr take);
    bool doRender(bool sync, juce::String* err);
    void guarded(const std::function<void()>& f);
    void finishRecording(bool fromAudioThreadCap);

    mutable std::mutex lock;
    Params prm; SlotPtr src[3]; ResultPtr res; juce::String stat, ui; juce::uint32 statT = 0;
    std::deque<Snap> undoStack, redoStack; bool inGesture = false, gesturePushed = false; juce::String lastKey; juce::uint32 lastKeyMs = 0;
    std::deque<std::function<void()>> tasks;
    std::atomic<bool> renderWanted { false }, cancelFlag { false }, busyTask { false };
    std::atomic<int> cancelGen { 0 }, contentVersion { 0 }; std::atomic<juce::uint32> serial { 0 }, nextId { 1 }; std::atomic<double> jobStart { 0 };
    juce::WaitableEvent wake;
    // recorder (audio thread writes rec / recLen; the message thread reads after recording is false)
    std::vector<float> rec; std::atomic<int> recLen { 0 }; std::atomic<bool> recording { false }, countingIn { false }, recFull { false }, recDone { false };
    std::atomic<int> countLeft { 0 }, recSlot { 0 }; std::atomic<float> inLevel { 0 }; double countPhase = 0, countBpm = 100; int countBeats = 0, clickLeft = 0; double clickPhase = 0; float clickAmp = 0; double clickHz = 1000;
    // preview (audio thread)
    struct PlayItem { ResultPtr r; TakePtr a, b, c; float gA = 1, gB = 1, gC = 1; };
    Handoff<PlayItem> items; std::mutex publishLock;
    double hostRate = 48000.0, pos = 0; int cur = PlayNone; float fade = 0; int pendingWhat = PlayNone; double pendingFrom = 0; bool switching = false;
    std::atomic<int> playReq { 0 }; int playSeen = 0; std::atomic<double> playFrom { 0.0 }; std::atomic<int> playWhatReq { PlayNone };
};
} // namespace fusion
