#pragma once
// ARCS page (Ghosty Spectrum): the processor-side owner. Holds the page bank (8 pages A..H, each with its own undo), the
// Engine (audio thread), save/load, MIDI export, the background audio render and the host tempo/transport interface.
// See ArcsPage.h for how the Spectrum owner wires it in.
#include "ArcsEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <deque>

namespace arcs {

// a note of the rendered page (one per audible line / repeat): same fields as Arrange's arr::Note (seconds from the start)
struct RenderNote { int pitch = 60; float velocity = 1.0f; double start = 0, length = 0.5; };
// the current page frozen for an offline render (cheap to copy; safe to hand to a worker thread)
struct RenderRequest { std::shared_ptr<const Snapshot> snap; double bpm = 120; juce::String name; bool valid() const { return snap != nullptr; } };

class ArcsHost : private juce::Timer {
public:
    using RenderRequest = arcs::RenderRequest;  // also spelled ArcsHost::RenderRequest
    ArcsHost();                                 // construct on the message thread (its timer frees retired snapshots)
    ~ArcsHost() override;

    // ---- audio thread (no allocation, no locks)
    void prepare(double sampleRate, int maxBlock);
    void process(float* L, float* R, int n, const HostInfo& host, bool add = true, const juce::MidiBuffer* midi = nullptr);
    // channel 0 / 1; a mono bus gets the page folded to mono at -3 dB (same loudness as stereo)
    void process(juce::AudioBuffer<float>& buffer, const HostInfo& host, bool add = true, const juce::MidiBuffer* midi = nullptr);
    static HostInfo readPlayHead(juce::AudioPlayHead* ph);                                    // tempo, ppq, seconds, playing
    static constexpr int latencySamples() { return Engine::kLatency; }

    // ---- message thread: the page being edited (the bank's current slot)
    const Page& page() const { return bank[(size_t) cur].page; }
    Page& edit() { return bank[(size_t) cur].page; }   // change it, then call changed() (live, no undo step) or commit() (undo step)
    void changed();                             // publish the page as it is now (rebuilds only the lines that changed)
    void commit(const juce::String& what = {}); // publish + one undo step (the step goes back to the last committed page)
    bool canUndo() const { return !bank[(size_t) cur].undo.empty(); }
    bool canRedo() const { return !bank[(size_t) cur].redo.empty(); }
    bool undo();
    bool redo();
    void clearHistory() { auto& s = bank[(size_t) cur]; s.undo.clear(); s.redo.clear(); s.committed = s.page; }
    juce::uint32 editCount() const { return edits; }   // bumps on every publish / slot change (UI refresh, stale render)
    double lastPublishMs() const { return publishMs; }
    int lastRebuiltLines() const { return rebuiltLines; }
    // ---- page bank A..H (MIDI notes 0..7 switch it too while MIDI-in is on)
    int slot() const { return cur; }
    void selectSlot(int s);
    bool slotEmpty(int s) const { return bank[(size_t) juce::jlimit(0, kSlots - 1, s)].page.arcs.empty(); }

    // ---- state / files
    juce::ValueTree getState() const;           // type "ARCSPAGE" (the current page) + child "BANK" (the other pages) + UI prefs
    void setState(const juce::ValueTree& t);    // any thread: off the message thread it is applied there asynchronously
    bool saveToFile(const juce::File& f) const; // the current page
    bool loadFromFile(const juce::File& f);     // into the current slot, undoable; refuses newer versions
    juce::String takeMessage() { auto m = message; message.clear(); return m; }   // load / export warnings for the UI
    bool dark = false;                          // UI theme (saved with the state)

    // ---- tempo
    double bpm() const { const double b = lastHostBpm.load(); return b > 1 ? b : page().ownBpm; }
    bool hostHasTempo() const { return lastHostBpm.load() > 1; }

    // ---- MIDI export: one note per audible line (per repeat), glides as pitch bend.
    //  standard: channels 1..16 except 10, bend +-12 / 24 / 48 (page bendRange), CC11 = envelope, CC10 = pan
    //  MPE:      lower zone per track (master ch 1, members 2..16), bend +-24 / 48 / 96, channel pressure = envelope, CC74 = pan
    //  A glide wider than the bend range is split into consecutive notes on the same channel; more overlapping lines than
    //  channels go to extra tracks (never two sounding notes on one channel).
    struct MidiReport { int notes = 0, splits = 0, tracks = 1; };
    juce::MidiFile makeMidi(bool mpe, double bpm, MidiReport* report = nullptr) const;
    juce::File writeMidi(const juce::File& f, bool mpe, MidiReport* report = nullptr) const;
    // ---- LOOP export (the loop brace's WAV / MIDI grips): only the loop range of one pass, re-timed to start at 0
    static bool loopSeconds(const Snapshot& s, double bpm, double& t0, double& t1);         // false = no loop on the page
    juce::MidiFile makeLoopMidi(bool mpe, double bpm, MidiReport* report = nullptr) const;   // notes that start inside the loop (ends clamped)
    juce::File writeLoopMidi(const juce::File& f, bool mpe, MidiReport* report = nullptr) const;
    static bool renderLoopToBuffer(const RenderRequest& r, juce::AudioBuffer<float>& out, double sampleRate, const std::atomic<bool>* abort = nullptr);   // any thread
    int bendRange(bool mpe) const { static const int S[] = { 12, 24, 48 }, M[] = { 24, 48, 96 }; return (mpe ? M : S)[(size_t) juce::jlimit(0, 2, page().bendRange)]; }
    static constexpr int kPpq = 960;

    // ---- background offline audio render (one pass of the page, loop ignored, reverse + speed lane honoured), streamed to
    // a 24-bit WAV at the host's sample rate (0 = the engine's current rate)
    bool startRender(const juce::File& wavOut, double sampleRate = 0);
    // ---- render to memory (SEND TO ARRANGE etc.): freeze the page on the message thread, render on any thread
    RenderRequest prepareRender() const;                                             // message thread
    static bool renderToBuffer(const RenderRequest& r, juce::AudioBuffer<float>& out, double sampleRate, std::vector<RenderNote>* notes = nullptr,
                               const std::atomic<bool>* abort = nullptr, std::atomic<int>* progress = nullptr);   // any thread; false = aborted / out of memory
    bool renderToBuffer(juce::AudioBuffer<float>& out, double sampleRate, std::vector<RenderNote>* notes = nullptr) { return renderToBuffer(prepareRender(), out, sampleRate, notes); }   // message thread, blocking
    static std::vector<RenderNote> notesOf(const Snapshot& s, double bpm);
    // ---- re-render from a saved state (ARRANGE's RE-RENDER): pure functions of the state, no live page, safe on any thread
    juce::MemoryBlock renderBlob() const;                                  // message thread: getState() + the tempo used, as binary (arr::SourceRef::state)
    static RenderRequest fromState(const juce::MemoryBlock& blob, double bpm = 0);   // bpm 0 = the tempo stored in the blob (else the page's own)
    static RenderRequest fromState(const juce::ValueTree& state, double bpm = 0);    // the same from getState() itself
    // damaged / unknown / newer-version state gives a valid request for an EMPTY page (renders silence), never a crash
    bool rendering() const { return job != nullptr && !jobDone.load(); }
    int renderProgress() const { return progress.load(); }
    bool renderReady() const { return job != nullptr && jobDone.load() && jobOk.load(); }
    juce::File renderedFile() const { return renderReady() ? renderTarget : juce::File(); }
    juce::String renderError() const { return jobDone.load() && !jobOk.load() ? jobError : juce::String(); }
    juce::uint32 renderedEdit() const { return renderEdit; }
    void cancelRender();

    Engine engine;
    std::atomic<double> lastHostBpm { 0.0 };

private:
    void timerCallback() override;
    void applyState(const juce::ValueTree& t);
    void publishSlot(int s);
    struct Slot { Page page, committed; std::deque<Page> undo, redo; };
    std::array<Slot, kSlots> bank; int cur = 0;
    mutable std::array<SnapCache, kSlots> caches;
    static constexpr size_t kUndoDepth = 64;
    juce::uint32 edits = 0; double publishMs = 0; int rebuiltLines = 0;
    juce::String message;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::vector<float> scratchL, scratchR;
    // render job
    struct Job; std::unique_ptr<Job> job;
    std::atomic<bool> jobDone { false }, jobOk { false }, jobAbort { false };
    std::atomic<int> progress { 0 };
    juce::File renderTarget; juce::uint32 renderEdit = 0; juce::String jobError;
};

} // namespace arcs
