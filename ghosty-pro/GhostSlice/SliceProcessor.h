#pragma once
// GHOST SLICE processor (GENERATE): the dropped / recorded sound, the takes made from it, and the playback.
//   message thread  load / record a sound, GENERATE / VARY (queued on one worker thread: analysis + material once per sound, then
//                   each take), select / delete / star takes, the controls, exports (the editor asks the demo gate first; these
//                   re-check the licence silently), the session state (the sound's path + every take's recipe: a reopened session
//                   re-renders the takes, deterministic per seed).
//   audio thread    MIDI (+ the on-screen keyboard) -> the voice engine (the current take) -> FX (warmth, tone, ensemble, space, width)
//                   -> the true-peak ceiling (-1.2 dBTP) -> demo drop-out. RECORD copies the track input into a buffer made before
//                   recording starts. No allocation, no locks.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Generate.h"
#include "GenerateEngine.h"
#include "SliceMic.h"
#include "Export.h"
#include "License.h"
#include "ExportGate.h"
#include "CleanStage.h"
#include <mutex>

class SliceProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SliceProcessor();
    ~SliceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghost Slice"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---------------------------------------------------------------- the sound
    void loadAudioAsync (const juce::File& f);
    bool loadAudio (const juce::File& f, juce::String& error);                     // (sync: tests)
    bool loadSourceForTests (std::shared_ptr<const gslice::SourceAudio> s);
    std::shared_ptr<const gslice::SourceAudio> getSource() const { return source; }
    bool isLoading() const { return loading.load(); }
    std::shared_ptr<const gslice::gen::Prepared> getPrepared() const { std::lock_guard<std::mutex> l (prepMutex); return prepared; }
    gslice::gen::Key detectedKey() const;                                          // the sound's key (C MIN until it is known)
    bool keyKnown() const { return getPrepared() != nullptr; }

    // ---------------------------------------------------------------- takes
    // take null = rendered when picked; trim0 / trim1 = the part of the sound (0..1) it was made from (TRIM)
    struct Slot { gslice::gen::Recipe recipe; std::shared_ptr<const gslice::gen::Take> take; int index = 1; double trim0 = 0.0, trim1 = 1.0; };
    const std::vector<Slot>& takes() const { return slots; }
    int currentIndex() const { return current; }
    std::shared_ptr<const gslice::gen::Take> currentTake() const { return current >= 0 && current < (int) slots.size() ? slots[(size_t) current].take : nullptr; }
    void generate();                         // GENERATE: a new take (AUTO rotates the type)
    void vary();                             // VARY: the current take's structure, new details
    void selectTake (int i);
    void stepTake (int d) { if (! slots.empty()) selectTake (juce::jlimit (0, (int) slots.size() - 1, current + d)); }
    void deleteTake (int i);
    void toggleFavourite (int i);
    gslice::gen::Type slotType (int i) const;
    juce::String slotKey (int i) const;
    bool busy() const { return jobs.load() > 0; }
    float progress() const { return prog.load(); }
    juce::String busyText() const { std::lock_guard<std::mutex> l (prepMutex); return busyWhat; }
    bool waitIdle (int timeoutMs);           // tests: run the message loop until the worker is done
    // ---------------------------------------------------------------- the parts the current take uses (the boxes on the waveform)
    using Parts = std::vector<std::pair<double, double>>;   // seconds of the whole sound, sorted, not overlapping
    Parts takeParts() const;                 // the current take's boxes: the user's when edited, else the generator's (overlaps joined)
    bool partsEdited() const;                // the current take was made from the user's boxes
    int maxParts() const;                    // how many boxes the current take's type uses
    juce::Range<double> partLimits() const;  // where boxes may go (seconds): the take's TRIM part of the sound
    // the current take re-renders from these parts (same recipe and seed; off the audio thread; the old take plays until the new
    // one is ready). Each edit can be undone. what = a short name for the toast ("move", "resize", ...)
    bool editParts (Parts parts, const juce::String& what = {});
    bool undoEdit();                         // Cmd / Ctrl + Z
    bool canUndo() const { return ! undoSteps.empty(); }
    void playRegion (double s0, double s1);  // hear a part of the sound (seconds), dry
    std::atomic<bool> lastRenderOnMessageThread { false };   // (tests: where the last take was rendered)

    // ---------------------------------------------------------------- controls (message thread; saved with the session)
    gslice::gen::Type type = gslice::gen::Type::Auto;
    double musical = 0.7;                    // 0 NATURAL .. 1 MUSICAL
    int keyTonic = -1; bool keyMinor = false;   // -1 = AUTO (the detected key)
    gslice::gen::Character character = gslice::gen::Character::Natural;
    double ownBpm = 120.0;                   // the standalone's tempo
    bool keyLock = false;                    // the SCALE lock: keep KEY when a new sound loads (off: a new sound starts on AUTO)
    // CLEAR: the next takes (GENERATE / VARY) are made for fidelity (Generate.h). On for a new Ghost Slice; a session saved before
    // CLEAR existed reopens with it off (its takes sound as they did). A change never re-renders a take: press GENERATE.
    bool clear = true;
    // TRIM: the part of the sound GENERATE uses (0..1 of its length; saved with the session). A change listens to that part again.
    double trimStart() const { return trim0; }
    double trimEnd() const { return trim1; }
    bool isTrimmed() const { return trim0 > 1e-6 || trim1 < 1.0 - 1e-6; }
    void setTrim (double a, double b);
    // build 6: the third knob (INSTRUMENT VOICE, PAD DREAM, CHOPS STUTTER, TEXTURE DRIFT), one value per type; the melody style; the
    // UNDER THE HOOD knobs (4 per type). -1 = the type's default. A change re-renders the take on screen when it is of that type
    // (rerender: the editor passes true when a drag ends / a menu item is picked), the old one plays until the new one is ready.
    std::array<double, 5> knobs { -1, -1, -1, -1, -1 };
    std::array<std::array<double, gslice::gen::Recipe::kDeep>, 5> deeps = [] { std::array<std::array<double, gslice::gen::Recipe::kDeep>, 5> d {}; for (auto& x : d) x.fill (-1.0); return d; }();
    gslice::gen::Style style = gslice::gen::Style::Auto;
    int lengthBars = 4;                      // LENGTH: 1 / 2 / 4 / 8 / 16 bars (the next takes; a change remakes the take on screen)
    void setLength (int bars, bool rerender);
    void stepStyle (int d);                  // STYLE < >: the previous / next style (in the browser's order), the take on screen remade
    static std::vector<gslice::gen::Style> styleOrder();   // AUTO, then every style group by group (the STYLE browser's order)
    // the STYLE browser's audition: two bars of the style's melody over its chords, on a soft bell-like tone, in the key the next take
    // uses (made on the message thread in a few ms; played dry, after the FX). Plays until done or another one starts.
    void auditionStyle (gslice::gen::Style s);
    void stopAudition();
    bool auditioning() const { return audPlay.load() != nullptr; }
    gslice::gen::Type knobType() const;      // the type the knobs speak for: TAKE TYPE, or the current take's under AUTO
    double knobValue() const;                // 0..1 (the default when untouched)
    double deepValue (int i) const;
    void setKnob (double v, bool rerender);
    void setDeep (int i, double v, bool rerender);
    void setStyle (gslice::gen::Style s, bool rerender);
    float takeLevel (int ch) { auto& a = ch == 0 ? outL : outR; const float v = a.load(); a.store (v * 0.8f); return v; }   // (UNDER THE HOOD meters, message thread)
    void setTone (float t) { fx.tone.store (juce::jlimit (-1.0f, 1.0f, t)); }
    void setSpace (float s) { fx.space.store (juce::jlimit (0.0f, 1.0f, s)); }
    float tone() const { return fx.tone.load(); }
    float space() const { return fx.space.load(); }
    double tempo() const;                    // the DAW's while it sends one, else ownBpm
    bool tempoFromDaw() const { return isHosted() && hostBpm.load() >= 20.0; }
    bool isHosted() const { return wrapperType != juce::AudioProcessor::wrapperType_Standalone; }

    // ---------------------------------------------------------------- audition
    void playPreview (bool on) { if (on) { playSource (false); stopAudition(); } engine.playPreview (on); }
    bool previewPlaying() const { return engine.previewPlaying(); }
    float previewPosition() const { return engine.previewPosition(); }
    void playSource (bool on);               // the sound itself (its TRIM part), dry
    bool sourcePlaying() const { return srcPlay.load() != nullptr; }
    double sourcePosition() const;          // seconds into the sound (-1 = not playing)
    float lastVelocity() const { return lastVel.load(); }     // the last note-on (MIDI keys or the on-screen keys), 0..1
    int noteOnCount() const { return noteOns.load(); }
    juce::MidiKeyboardState keyboard;        // the on-screen keys
    int activeVoices() const { return engine.activeVoices(); }

    // ---------------------------------------------------------------- RECORD
    enum class Input { Auto = 0, Track = 1, Mic = 2 };
    Input input = Input::Auto;
    bool startRecording();
    void stopRecording();                    // keeps it (the recording becomes the sound)
    bool recording() const { return recOn.load(); }
    double recordedSeconds() const;
    float inputLevel() const { return level.load(); }
    juce::String recordSourceText() const;
    static constexpr double kMaxRecordS = 60.0;

    // ---------------------------------------------------------------- exports (the editor asked the gate; these re-check the licence)
    juce::File writeTakeWav (const juce::File& dir, juce::String& error);   // the take through the FX, -14 LUFS, true peak < -1 dBTP
    juce::File writeTakeMidi (const juce::File& dir, juce::String& error);
    juce::File saveTake (juce::String& error);                               // WAV + MIDI into takesFolder()
    static juce::File takesFolder();
    static juce::File recordingsFolder();
    static juce::File dragFolder();
    juce::String takeFileName() const;       // "Ghost Slice - Take 3 - PAD in A MIN"

    ghosty::License license { "Ghost Slice" };
    ghosty::DemoGate demo;
    std::atomic<int> uiTheme { 1 };          // 0 BLACK, 1 PURPLE, 2 LIGHT, 3 GHOST
    juce::String lastMessage;
    std::function<void()> onChanged;         // the editor refreshes (message thread)
    std::function<void (const juce::String&)> onToast;
    void unlockedNow();
    gslice::gen::Engine& getEngine() { return engine; }
    gslice::gen::Fx& getFx() { return fx; }
    int ceilingLatency() const { return ceiling.latency(); }
    std::atomic<long> audioAllocGuard { 0 };

private:
    void timerCallback() override;
    void installSource (std::shared_ptr<const gslice::SourceAudio> s, bool keepTakes, bool restoring = false);   // restoring: the session's TRIM and KEY stay
    static std::shared_ptr<const gslice::gen::Prepared> prepareFor (std::shared_ptr<const gslice::SourceAudio> src, double t0, double t1, const gslice::gen::Progress& p);
    std::shared_ptr<const gslice::gen::Prepared> preparedFor (double t0, double t1) const;   // the cached one when it is for this part
    void queuePrepare();
    void queueRender (int slotIndex, bool makeCurrent, bool playWhenDone);
    gslice::gen::Recipe recipeNow() const;
    void applyKnobs (gslice::gen::Recipe& r, gslice::gen::Type t) const;
    bool rerenderCurrentFor (gslice::gen::Type t);
    std::atomic<float> outL { 0.0f }, outR { 0.0f };
    std::atomic<const juce::AudioBuffer<float>*> audPlay { nullptr }; std::shared_ptr<juce::AudioBuffer<float>> audHold, audOld; double audOldAt = 0;
    std::atomic<int> audPos { 0 };
    void publishCurrent();
    void trimMemory();
    void changed() { if (onChanged) onChanged(); }
    void toast (const juce::String& s) { lastMessage = s; if (onToast) onToast (s); }
    std::shared_ptr<const gslice::SourceAudio> source;
    mutable std::mutex prepMutex;
    std::shared_ptr<const gslice::gen::Prepared> prepared;
    double prepT0 = 0.0, prepT1 = 1.0;       // the part of the sound `prepared` was made from
    double trim0 = 0.0, trim1 = 1.0;
    // the sound's own playback (audio thread reads srcPlay; the holder keeps it alive, an old one is let go a while later)
    std::atomic<const gslice::SourceAudio*> srcPlay { nullptr };
    std::shared_ptr<const gslice::SourceAudio> srcPlayHold, srcPlayOld; double srcPlayOldAt = 0;
    std::atomic<double> srcPos { 0.0 }, srcEnd { 0.0 };
    std::atomic<float> lastVel { 0.0f }; std::atomic<int> noteOns { 0 };
    juce::String busyWhat;
    std::vector<Slot> slots; int current = -1; int takeCounter = 0, rotation = 0;
    std::vector<int> renderedOrder;          // slot indices with audio, most recently used last
    struct UndoStep { int index = 0, variation = 0; uint32_t seed = 0, detail = 0; Parts parts; };
    std::vector<UndoStep> undoSteps;
    int slotFor (int index, int variation, uint32_t seed, uint32_t detail) const;
    juce::ThreadPool pool { 1 };
    std::atomic<int> jobs { 0 }; std::atomic<float> prog { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::shared_ptr<std::atomic<bool>> sourceGen = std::make_shared<std::atomic<bool>> (true);   // (replaced on every new sound: old jobs drop their results)
    gslice::gen::Engine engine;
    gslice::gen::Fx fx;
    ghosty::clean::Ceiling ceiling;
    std::atomic<double> hostBpm { 0.0 }, hostSr { 48000.0 };
    std::atomic<bool> loading { false };
    int maxBlock = 512;
    // RECORD
    gslice::MicInput mic;
    juce::AudioBuffer<float> recTrack, recMic;           // made before recording starts
    std::atomic<int> recTrackPos { 0 }, recMicPos { 0 };
    std::atomic<bool> recOn { false }; std::atomic<float> level { 0 }; std::atomic<float> trackPeak { 0 }, micPeak { 0 };
    double recTrackRate = 48000, recMicRate = 48000;
    juce::MemoryBlock pendingState;          // a session restored while locked (the demo reopens empty)
    juce::MidiBuffer kbMidi;
    struct KeyFifo; std::unique_ptr<KeyFifo> keyFifo;    // the on-screen keys -> the audio thread (lock-free)
    juce::String waitingFor;                 // a restored session's missing sound (its fingerprint)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceProcessor)
};
