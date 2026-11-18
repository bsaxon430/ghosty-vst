// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// CHOP (build 8, Brandon 2026-11-13): Ghost Voice is a channel-rack instrument. Drag a WAV in (or record one), edit it with
// FL Studio-style wave tools, chop it, play it on the keys, and convert the voice of any part of it.
//
// The sound engine is INTERSECT (tucktuckg00se, GPL-3.0): its untouched code (third_party/intersect) loads the clip, holds the
// slices, plays them from MIDI with its own stretch engines. Ghost Voice only drives it through INTERSECT's own public command API
// (pushCommand / loadFileAsync), the same way INTERSECT's own editor does. The tempo, key, transients and chop points come from
// Flayr Chop's analysis engine (FlayrLabs, AGPL-3.0, third_party/flayr-chop/Analysis.h, untouched).
//
// What is ours: the clip editor (select / loop / cut / copy / paste / delete / trim / duplicate, undo), the clip's
// transport (SPACE plays, LOOP loops), the KEYS modes (CHOPS: one chop per key from C1, WHOLE: the clip pitched across the
// keyboard), AUTO CHOP's RANDOM mode (random whole beats, 1 or 2 beats long, snapped to the hits) and CONVERT VOICE.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>
#include <vector>
#include "ChopTools.h"

class IntersectProcessor;

namespace gv::chop
{
struct Clip
{
    juce::AudioBuffer<float> audio;   // 1 or 2 channels
    double rate = 44100.0;
    juce::String name;
    juce::int64 frames() const { return audio.getNumSamples(); }
    double seconds() const { return rate > 0 ? (double) frames() / rate : 0.0; }
};
using ClipPtr = std::shared_ptr<const Clip>;

struct Region
{
    juce::int64 a = 0, b = 0;
    bool empty() const { return b <= a; }
    juce::int64 length() const { return juce::jmax ((juce::int64) 0, b - a); }
    bool operator== (const Region& o) const { return a == o.a && b == o.b; }
};

// what Flayr Chop's analysis found in the clip
struct Detected
{
    bool ready = false;
    double bpm = 0, firstBeat = 0;
    float bpmConfidence = 0;
    int key = 0; bool minor = false; float keyConfidence = 0;
    std::vector<double> onsets; std::vector<float> strength;
    juce::String keyName() const;
    juce::String summary() const;     // "120 BPM  ·  E minor" (a ? when Flayr is unsure)
};

enum class AutoMode { random = 0, hits, beats, twoBeats, bar, even, phrases, words, kModes };
inline bool usesTool (AutoMode m) { return m == AutoMode::phrases || m == AutoMode::words; }
juce::String autoModeName (AutoMode);
juce::String autoModeTip (AutoMode);
enum class KeysMode { chops = 0, whole = 1 };

// a slice as INTERSECT holds it, in clip frames
struct SliceView { Region r; int note = 36, highNote = 36; int index = 0; };

class ChopEngine : private juce::Timer
{
public:
    ChopEngine();
    ~ChopEngine() override;
    IntersectProcessor& intersect() { return *engine; }

    // ---- audio thread
    void prepare (double sampleRate, int blockSize);
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& hostMidi, juce::AudioPlayHead*);

    // ---- the clip (message thread)
    bool loadFile (const juce::File&, juce::String& why);
    void setClip (juce::AudioBuffer<float> audio, double rate, const juce::String& name);   // takes an undo step
    ClipPtr clip() const { return std::atomic_load (&current); }
    bool hasClip() const { auto c = clip(); return c != nullptr && c->frames() > 0; }
    juce::File clipFile() const { return currentFile; }
    juce::int64 frames() const { auto c = clip(); return c ? c->frames() : 0; }
    double rate() const { auto c = clip(); return c ? c->rate : 44100.0; }
    juce::File writeRegion (Region, const juce::String& stem) const;   // a WAV of that part of the clip (CONVERT VOICE, drag out)

    // ---- FL-style edits (message thread). Each is one undo step; false = nothing to do.
    bool copy (Region);
    bool cut (Region);
    bool erase (Region);                      // DELETE: removes the audio, the rest closes up
    bool paste (juce::int64 at, Region replace = {});   // pastes the clipboard at `at` (over `replace` when it isn't empty)
    bool trim (Region);                       // keeps only this part
    bool duplicate (Region);                  // Cmd+B: a copy right after it
    bool replaceRegion (Region, const juce::AudioBuffer<float>& with, double withRate);   // CONVERT VOICE puts its take back here
    bool hasClipboard() const { return clipboard.getNumSamples() > 0; }
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // ---- chops + keys
    std::vector<Region> chops;                // the chops (clip frames), one per key from rootNote in CHOPS mode
    std::vector<juce::String> labels;         // what each chop says (WORDS), same order as chops; empty = nothing
    KeysMode keys = KeysMode::whole;
    AutoMode autoMode = AutoMode::random;
    int chopCount = 16;
    bool autoChop();                          // AUTO CHOP with autoMode (waits for the analysis when it isn't in yet)
    Tools tools;                              // PHRASES / WORDS (build 9)
    bool toolReady() const { return ! usesTool (autoMode) || tools.ready (toolKind()); }
    Tools::Kind toolKind() const { return autoMode == AutoMode::words ? Tools::Kind::words : Tools::Kind::phrases; }
    bool installTool();                       // INSTALL the tool autoMode needs
    bool toolBusy() const { return tools.busy(); }
    juce::String labelFor (size_t chop) const { return chop < labels.size() ? labels[chop] : juce::String(); }
    void setKeys (KeysMode);
    void splitAt (juce::int64 frame);         // SLICE tool: cuts the chop under the point in two (or starts chopping the clip there)
    bool removeChopAt (juce::int64 frame);    // DELETE tool on a chop
    void clearChops();
    std::vector<SliceView> slices() const;    // what INTERSECT holds now (its own editor can change them too)
    // one chop's sound: INTERSECT's own per-slice settings (field = IntersectProcessor::Field*), read back from its snapshot
    float sliceParam (int sliceIndex, int field) const;
    void setSliceParam (int sliceIndex, int field, float value);
    int sliceCount() const;
    static constexpr int rootNote = 36;       // C1, INTERSECT's own first note (Flayr Chop's pad A1 too)

    // ---- the clip's transport (SPACE / LOOP)
    void play (juce::int64 from, Region loopRegion, bool loop);
    void stop();
    bool isPlaying() const { return playing.load(); }
    juce::int64 playhead() const { return (juce::int64) playPos.load(); }
    // the keys (the on-screen piano, audition)
    void noteOn (int note, float velocity);
    void noteOff (int note);
    bool noteSounding (int note) const;

    Detected detected() const;
    bool analysing() const { return analysisBusy.load(); }
    bool engineLoaded() const;                // INTERSECT has the current clip in

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);
    std::function<void()> onChange;           // message thread
    juce::String message;                     // the last thing worth telling (shown on the status line)

    static juce::File folder();               // <workDir>/chop
    static constexpr size_t kUndoSteps = 24;

private:
    void timerCallback() override;
    struct Snapshot { ClipPtr clip; juce::File file; std::vector<Region> chops; KeysMode keys; std::vector<juce::String> labels; };
    Snapshot snap() const { return { clip(), currentFile, chops, keys, labels }; }
    int toolSerial = 0;
    void pushUndo();
    void restore (const Snapshot&);
    void install (ClipPtr, bool writeFile);   // new clip: file, INTERSECT, analysis
    void analyse();
    void rebuildSlices();                     // our chops / keys -> INTERSECT's slices
    void changed() { if (onChange) onChange(); }
    juce::int64 toEngine (juce::int64 clipFrame) const;
    juce::int64 toClip (juce::int64 engineFrame) const;

    std::unique_ptr<IntersectProcessor> engine;
    ClipPtr current;
    juce::File currentFile;
    juce::AudioBuffer<float> clipboard; double clipboardRate = 44100.0;
    std::vector<Snapshot> undoStack, redoStack;
    bool pendingRebuild = false, pendingAutoChop = false;
    int loadSerial = 0;
    bool rebuildPushed = false; juce::uint32 pushedVersion = 0;

    // analysis (Flayr Chop), on its own thread
    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    mutable juce::CriticalSection detLock;
    Detected det;
    std::atomic<bool> analysisBusy { false };
    int analysisSerial = 0;

    // audio
    double sr = 48000.0; int maxBlock = 512;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer engineMidi;
    juce::MidiMessageCollector keysIn;
    std::atomic<bool> playing { false }, looping { false };
    std::atomic<double> playPos { 0 };
    std::atomic<juce::int64> loopA { 0 }, loopB { 0 };
    std::atomic<juce::uint64> sounding[2] {};
    juce::Random rng;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopEngine)
};
}
