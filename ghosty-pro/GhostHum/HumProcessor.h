#pragma once
// GHOST HUM - the plugin. Records from its audio input (the mic in the standalone app, the track it sits on in a DAW) or, when the host
// gives it no usable audio (FL Studio's channel rack: an instrument gets no input), straight from the Mac's microphone (HumMic.h; build 3),
// turns the recording into notes or drum hits on a background thread (HumMelody.h / HumBeat.h), plays them on the built-in sounds
// (HumSounds.h), sends them to the host as MIDI while it plays, and keeps the take + settings in the project. Parameters (APVTS) are the
// knobs a host may automate; the take itself lives in the state as JSON (HumModel.h).
//
// INPUT (build 3): AUTO (default) takes the track whenever it carries sound and falls back to the Mac's microphone when the host has no
// input channels or feeds digital silence for the first half second of a take; TRACK and MAC MIC force one. Both sources write their own
// lock-free buffer on their own audio thread; the message thread decides (pollSources) and copies the chosen take at stop.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "HumMic.h"
#include "HumModel.h"
#include "HumMelody.h"
#include "HumBeat.h"
#include "HumTempo.h"
#include "HumSounds.h"
#include "HumFiles.h"
#include "License.h"
#include "TempoMul.h"
#include "ExportGate.h"
#include "GhostThemes.h"
#include "Bypass.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace hum {

enum InputChoice { InputAuto = 0, InputTrack = 1, InputMic = 2 };   // the INPUT switch
enum RecSource { SrcNone = 0, SrcHost = 1, SrcMic = 2 };             // who owns the take being recorded (SrcNone: AUTO still deciding)

class HumProcessor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::Timer {
public:
    HumProcessor();
    ~HumProcessor() override;

    // ---- JUCE
    void prepareToPlay(double sr, int block) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghost Hum"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Ghost Hum"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam(); }
    juce::AudioParameterBool* bypassParam() const { return dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass")); }

    // ---- parameters
    juce::AudioProcessorValueTreeState apvts;
    int   mode() const { return (int) p("mode")->load(); }
    int   quant() const { return (int) p("quant")->load(); }
    float quantStrength() const { return p("qstrength")->load() / 100.0f; }
    double swing() const { return p("swing")->load(); }
    bool  tempoFree() const { return p("tempomode")->load() < 0.5f; }
    double setBpm() const { return p("bpm")->load(); }
    // TEMPO ½x | 1x | 2x (Shared/TempoMul.h), BPM mode: Ghost works at the set tempo times this (the grid, the click, the count-in, playback);
    // DRAG MIDI is written at the set (DAW) tempo with every time scaled, so it lands as heard. FREE mode follows your own timing (its
    // KEEP ORIGINAL / half / double offers do the same job). Saved with the project; a fresh start is 1x.
    std::atomic<int> tempoMul { ghosty::tempomul::Normal };
    double workBpm() const { return setBpm() * ghosty::tempomul::factor(tempoMul.load()); }
    void setTempoMul(int code);
    double midiScale() const { const auto& T = take(); return !tempoFree() && T.bpmKnown && tempoMul.load() != ghosty::tempomul::Normal && setBpm() > 0 ? T.bpm / setBpm() : 1.0; }
    bool  metronome() const { return p("metro")->load() > 0.5f; }
    bool  countIn() const { return p("countin")->load() > 0.5f; }   // BPM mode: one bar of clicks (4 3 2 1) before the recording starts
    int   voice() const { return (int) p("sound")->load(); }
    int   pitchMode() const { return (int) p("pitch")->load(); }
    bool  oldEars() const { return p("ears")->load() > 0.5f; }              // build 4: BUILD 3 ears chosen
    float smoothing() const { return p("smooth")->load() / 100.0f; }         // build 4: SMOOTH 0..1
    int   keyChoice() const { return (int) p("key")->load() - 1; }     // -1 = the detected key, else 0..11 (+12 minor)
    bool  playOriginal() const { return p("source")->load() > 0.5f; }
    int   inputChoice() const { return (int) p("input")->load(); }   // InputAuto / InputTrack / InputMic
    void  setParam(const char* id, float v) { if (auto* q = apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(v)); }
    Grid  grid() const { Grid g; g.quant = quant(); g.strength = quantStrength(); g.swing = mode() == Beatbox ? swing() : 50.0; return g; }

    // ---- the takes (message thread): one per mode (MELODY / BEATBOX), so switching modes never loses the other one (and multi-track later)
    std::array<Take, 2> takes; std::array<Undo, 2> undos; beat::Calibration cal;
    Take& take() { return takes[(size_t) juce::jlimit(0, 1, mode())]; }
    const Take& take() const { return takes[(size_t) juce::jlimit(0, 1, mode())]; }
    Undo& undo() { return undos[(size_t) juce::jlimit(0, 1, mode())]; }
    ghosty::themes::Theme theme = ghosty::themes::Theme::Purple;
    // GHOST ANIMATION (build 2): the window's ghosts, wisps, particles, ring crackle and logo pulse. Kept in the plugin state AND as an app
    // setting (Documents/Ghosty/Ghost Hum/settings.json), so a new instance opens the way the owner left the last one. Default ON.
    bool ghostAnim = true;
    void setGhostAnim(bool on) { ghostAnim = on; saveSettings(); changed(); }
    void saveSettings() const; void loadSettings();
    static juce::File settingsFile() { return files::exportDir("").getChildFile("settings.json"); }
    double hostBpm() const { return hostBpmA.load(); }   // the DAW's tempo while it plays (0 = none / standalone): the HOST pill sets BPM to it
    std::function<void()> onChanged;          // the editor refreshes
    juce::String status;                       // one line for the editor ("Heard 12 notes - C major - 96 BPM")

    // ---- recording (any thread may read the flags; start / stop from the message thread)
    void startRecording();
    void stopRecording();                      // -> analysis on a background thread -> finishAnalysis on the message thread -> plays once
                                               // (also called by handleAsyncUpdate when the 90 s cap stopped the recording by itself)
    int  recordingMode() const { return recMode; }   // the mode the current / last recording was started in (a take is analysed in that mode even if the switch moved)
    bool isRecording() const { return recording.load(); }
    bool isCountingIn() const { return countingIn.load(); }
    int  countInLeft() const { return countLeft.load(); }    // 4, 3, 2, 1 while counting in
    bool isAnalysing() const { return analysing.load(); }
    std::vector<double> lastClickTimes() const { std::vector<double> v; const int n = juce::jmin(clickCount.load(), (int) clickSec.size()); for (int i = 0; i < n; ++i) v.push_back(clickSec[(size_t) i]); return v; }
    double recordedSeconds() const { const double h = recLen.load() / juce::jmax(1.0, sampleRate), m = micLen.load() / juce::jmax(1.0, micRate.load()); const int s = src.load(); return s == SrcMic ? m : s == SrcHost ? h : juce::jmax(h, m); }
    float inputLevel() const { const float h = hostLevel.load(), m = micReady.load() ? micLevel.load() : 0.0f; const int s = src.load(); if (recording.load()) { if (s == SrcMic) return m; if (s == SrcHost) return h; } return juce::jmax(h, m); }   // the ring follows the source being recorded (between takes: whatever has sound)
    static constexpr double kMaxRecordSeconds = 90.0;
    static constexpr float kSilence = 1.0e-5f;   // under this peak an input is digital silence (-100 dBFS): a host feeding nothing, a mic with no permission. A live mic's noise floor is far above it.
    // ---- where the sound comes from (build 3)
    int  recordingSource() const { return src.load(); }      // SrcHost / SrcMic / SrcNone while AUTO is still deciding
    bool micOpen() const { return micReady.load(); }
    bool hostHasSound() const { return getTotalNumInputChannels() > 0 && juce::Time::getMillisecondCounter() - hostLastSignalMs.load() < 3000; }   // the track carried sound in the last 3 s (AUTO trusts it at the tap)
    void pollSources();                                        // message thread, 10 Hz while recording (the timer; tests call it): AUTO picks the source, "No sound is coming in" after 2 s
    void micIdle() { if (!recording.load() && !countingIn.load()) closeMic(); }   // the editor closed: let the mic go
    juce::String listeningText() const;                        // "Listening through your Mac's microphone... sing or hum. Tap again when you're done."
    // plain words when nothing arrives for 2 s (and at the stop of a take that stayed at digital zero). The mic case names the Mac's permission
    // prompt: macOS asks the host app (FL Studio / the Ghost Hum app) the first time, and a refused mic delivers exact silence.
    static juce::String noSoundText(int source) { return source == SrcMic ? "No sound is coming in from your Mac's microphone. Check it is plugged in and allowed (System Settings > Privacy & Security > Microphone), then tap again."
                                                                          : "No sound is coming in. Check your microphone (or put Ghost Hum on your mic's mixer track)."; }
    void micCallback(const float* const* in, int nCh, int n);  // the mic device's audio thread (tests feed it directly)
    // tests / the TEACH flow: analyse this audio now, on the calling thread, as if it had been recorded
    void analyseAudio(const std::vector<float>& x, double sr, bool play = false, std::vector<double> clicks = {}, int asMode = -1);
    std::vector<float> lastRecording() const { return recCopy(); }
    double lastRecordingRate() const { return recRate; }

    // ---- TEACH GHOST YOUR SOUNDS: set teachingDrum, record; stopRecording keeps up to 3 examples of that drum from what was heard
    int  teachingDrum = -1;                    // >= 0 while the next recording teaches that drum
    std::function<void(int drum, int got)> onTaught;
    int  teachFromRecording(int drum);         // returns how many examples it got (0 = heard nothing)
    void forgetTaught() { cal.ex.clear(); saveCalibration(); if (mode() == Beatbox) retranscribe(); }
    void saveCalibration() const; void loadCalibration();
    static juce::File calibrationFile() { return files::exportDir("").getChildFile("my-sounds.json"); }

    // ---- playback
    void play(); void stop(); bool isPlaying() const { return playing.load(); }
    double playPosition() const { return playSec.load(); }   // seconds of the take
    void auditionNote(int midi); void auditionDrum(int drum);

    // ---- edits (every one pushes undo)
    void beginEdit() { undo().push(take()); }       // before a piano-roll drag
    void edited() { sortTake(take()); rebuildSequence(); changed(); }
    void clean();
    bool doUndo(); bool doRedo();
    void chooseBpm(double bpm, bool keepOriginal);   // the FREE offers: [KEEP ORIGINAL] [94] [188]
    void retranscribe();                        // calibration changed: hear the last recording again (edits are lost, undoable)
    void repitch();                             // PITCH / KEY changed: the working notes again from the as-heard notes (edits are lost, undoable)
    void clearTake();

    // ---- export (the caller asked the gate)
    bool exportAllowed(juce::Component* ui, const char* id) const { return ghosty::exportAllowed(license, ui, id); }
    juce::File writeMidiFile(); juce::File writeWavFile();
    ghosty::License license { "Ghost Hum" };

    std::array<std::vector<float>, 2> recCopies; double recRate = 48000;   // the last recording per mode (hear it again after TEACH / PITCH changes; tests)
    std::array<std::vector<double>, 2> recClickss;                          // ... and when the metronome clicked during it
    std::vector<double>& recClicks(int m = -1) { return recClickss[(size_t) juce::jlimit(0, 1, m < 0 ? mode() : m)]; }
    std::vector<float>& recCopy(int m = -1) { return recCopies[(size_t) juce::jlimit(0, 1, m < 0 ? mode() : m)]; }
    const std::vector<float>& recCopy(int m = -1) const { return recCopies[(size_t) juce::jlimit(0, 1, m < 0 ? mode() : m)]; }
    std::atomic<int> uiCounter { 0 };                     // bumps on every change the editor should show
   #if GHOSTY_TEST_HOOKS
    bool testSync = true;                                 // tests: analyse on the calling thread, no background thread
    void handleAsyncUpdateForTests() { cancelPendingUpdate(); handleAsyncUpdate(); }   // tests: parameter changes land now
    struct FakeMic { bool available = false; double rate = 44100; bool none = false; } fakeMic;   // tests: a pretend microphone device (openMic succeeds at this rate; feed it with micCallback); none = no device at all (the open fails on any machine)
    void micFeed(const float* x, int n) { const float* chans[1] { x }; micCallback(chans, 1, n); }
   #endif

private:
    std::atomic<float>* p(const char* id) const { return apvts.getRawParameterValue(id); }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void handleAsyncUpdate() override;          // analysis done -> finishAnalysis
    void finishAnalysis(Take t, bool play);
    void rebuildSequence();
    void changed() { ++uiCounter; if (onChanged) onChanged(); }
    void parameterChanged(const juce::String& id);
    struct ParamListener : juce::AudioProcessorValueTreeState::Listener { HumProcessor& o; explicit ParamListener(HumProcessor& x) : o(x) {} void parameterChanged(const juce::String& id, float) override { o.parameterChanged(id); } } listener { *this };

    double sampleRate = 48000; int blockSize = 512;
    std::vector<float> rec; std::atomic<int> recLen { 0 }; std::atomic<bool> recording { false }, analysing { false }, playing { false };
    std::atomic<bool> recFull { false }; int recMode = 0;   // recFull: the 90 s cap stopped it on the audio thread (host or mic); the message thread finishes the take
    int recClock = 0;                                         // host samples since the take started (host audio thread): the metronome's clock, whoever owns the samples
    std::atomic<double> hostBpmA { 0 };
    std::atomic<float> hostLevel { 0 }; std::atomic<double> playSec { 0 };
    // the Mac's microphone (build 3): its own buffer at its own rate, written on the device thread while `micArmed` and recording
    MicInput mic; std::vector<float> micRec; std::atomic<int> micLen { 0 }; std::atomic<double> micRate { 0 }; std::atomic<bool> micArmed { false }, micReady { false };   // micReady: openMic succeeded (the device runs; in tests, the fake one)
    std::atomic<float> micLevel { 0 }, micTakePk { 0 }, hostTakePk { 0 };   // the loudest sample each source gave during this take (AUTO decides on them)
    std::atomic<int> src { SrcNone }; std::atomic<juce::uint32> hostLastSignalMs { 0 }; juce::uint32 micLastUsedMs = 0; bool noSoundSaid = false;
    bool armInput();                                          // the record tap: pick the source for INPUT / what the host offers; opens the mic when needed. false = refused (status says why)
    bool openMic(); void closeMic();
    void timerCallback() override;                            // pollSources while recording; closes an idle mic
    std::atomic<int> pendingMetroBeat { 0 }; double recBeatPhase = 0;
    std::atomic<bool> countingIn { false }; std::atomic<int> countLeft { 0 }; double countPhase = 0;
    std::array<double, 1024> clickSec {}; std::atomic<int> clickCount { 0 };   // when the metronome clicked during the take (seconds of the recording): the analysis masks them (speaker -> mic)
    void clickNow(bool accent, int atLen) { synth.click(accent); const int k = clickCount.load(); if (k < (int) clickSec.size()) { clickSec[(size_t) k] = atLen / juce::jmax(1.0, sampleRate); clickCount.store(k + 1); } }
    sound::Synth synth;
    std::shared_ptr<const std::vector<sound::Ev>> seq, seqPending, seqOld; juce::SpinLock seqLock; size_t seqPos = 0; double seqEnd = 0; bool wheelBent = false; bool rehearPending = false;   // seqOld: the audio thread parks the sequence it stopped using; the message thread frees it (no free on the audio thread)
    std::atomic<int> auditionType { -1 }, auditionNum { 0 }; int auditionOff = 0, auditionOffNum = 0;
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>>(true);
    std::unique_ptr<Take> analysed; bool analysedPlay = false; std::mutex analysedLock;
    std::atomic<bool> stopRequest { false }; std::atomic<int> dirty { 0 };   // parameter changes waiting for the message thread
    ghosty::BypassFader bypass; juce::AudioBuffer<float> dry;   // the input as it came (a DAW insert passes it through; BYPASS = exactly it)
    std::vector<juce::MidiMessage> midiOut;      // (audio thread scratch)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HumProcessor)
};

} // namespace hum
