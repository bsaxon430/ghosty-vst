#pragma once
#include "License.h"
#include "ExportGate.h"   // the one demo export gate (docs/DEMO-EXPORT-GATE.md)
#include "SpecEngine.h"
#include "SpecKey.h"
#include "LabHost.h"       // SPECTRAL LAB tab
#include "ArrangeEngine.h" // ARRANGE tab (arr::Session)
#include "ArcsHost.h"      // ARCS tab (arcs::ArcsHost)
#include "Bypass.h"
#include "GhostThemes.h"
#include "SyncTransport.h"   // SYNC: DAW | OWN
#ifndef GHOSTY_PROCESS_LAB
 #define GHOSTY_PROCESS_LAB 0     // PROCESS page (Shared/ProcessLab*): CMake option GHOSTY_PROCESS_LAB (default ON)
#endif
#if GHOSTY_PROCESS_LAB
 #include "ProcessLab.h"
#endif
#ifndef GHOSTY_VOICE
 #define GHOSTY_VOICE 0           // VOICE page (WORLD): CMake option GHOSTY_VOICE (default ON for Spectrum)
#endif
#if GHOSTY_VOICE
 #include "VoiceHost.h"
#endif
#include <bitset>        // shared BYPASS (fader + header button)
#include <juce_audio_utils/juce_audio_utils.h>

class SpecProcessor : public juce::AudioProcessor {
public:
    ghosty::License license { "Ghosty Spectrum" }; ghosty::DemoGate demo;   // paid version: license key + demo mode
    SpecProcessor();
    ~SpecProcessor() override;
    const juce::String getName() const override { return "Ghosty Spectrum"; }
    void prepareToPlay(double sr, int block) override {
        engine.prepare(sr, juce::jmax(64, block)); lab.prepare(sr, block); arrange.prepare(sr, juce::jmax(64, block)); arcsPage.prepare(sr, juce::jmax(64, block));
        pageMidi.ensureSize(8192); bypass.prepare(sr, 0.006, bypassParam->get());
        demo.prepare(sr);   // the demo drop-out: every 20 s at any rate (was the 44.1 kHz default period)
#if GHOSTY_PROCESS_LAB
        labAudition.prepare(sr);
#endif
#if GHOSTY_VOICE
        voice.prepare(sr, juce::jmax(64, block));
#endif
        gateBuf.setSize(2, juce::jmax(64, block)); gateGain.setSize(1, juce::jmax(64, block)); notesGate.prepare(sr, 0.006, playModeP->load() > 0.5f && heldCount == 0);
        sync.prepare(sr); syncSr = sr > 0 ? sr : 48000.0; syncFresh = true; syncG = 1.0f; syncDir = 0; ownSec = 0;
        demo.prepare(sr); publish(); }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
        const auto in = l.getMainInputChannelSet();   // the sidechain DETECT KEY listens to: off, mono or stereo
        return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; } void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; } void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    sp::Model model; std::shared_ptr<gb::DrumLibrary> drums;
    lab::LabHost lab;                                 // SPECTRAL LAB (its own canvas, worker and player)
    std::atomic<float>* labMorphP = nullptr, *labLevelP = nullptr, *labWidthP = nullptr, *labOutputP = nullptr, *labGhostP = nullptr, *labMuteP[lab::kMaxBands] {};
    void setLabParam(const char* id, float plainValue) { if (auto* p = apvts.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(plainValue)); p->endChangeGesture(); } }
    juce::AudioProcessorValueTreeState apvts; sp::SpecEngine engine;
    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* playModeP = nullptr;          // "play_mode": PLAY ALWAYS | ON NOTES (saved with the project, never in presets / lab state)
    std::atomic<bool> gateOpenUi { true };            // ON NOTES: a key is held (the header shows it)   // "bypass": the shared BYPASS (never in presets / snapshots / lab state)
    ghosty::BypassFader bypass;
#if GHOSTY_PROCESS_LAB
    plab::AuditionPlayer labAudition;                 // the PROCESS page's PLAY SRC / PLAY FX (rendered at the end of processBlock)
#endif
    // the PROCESS page's chain ("PLAB"), saved with the project (kept as-is by a build without the page); the audio is the Lab's source
    juce::ValueTree plabState() const { const juce::ScopedLock sl(plabLock); return plabTree.createCopy(); }
    void storePlabState(const juce::ValueTree& v) { const juce::ScopedLock sl(plabLock); plabTree = v.createCopy(); }
    std::atomic<int> plabGen { 0 };                   // bumped when a project brings a chain (the page re-loads it)
#if GHOSTY_VOICE
    voice::VoiceHost voice;                           // VOICE page: pitch / formant editing; its preview adds into the output (gated by PLAY / BYPASS)
    arr::SourceRegistration voiceSource;              // ARRANGE can re-render VOICE clips
    static arr::PayloadPtr voicePayload(const voice::Result& r, const juce::String& name, juce::MemoryBlock state);
#endif
    arr::Session arrange;                             // ARRANGE: clips made anywhere in Spectrum, sequenced (adds into the output)
    arcs::ArcsHost arcsPage;                          // ARCS: drawn lines = voices (adds into the output; its own 36-sample FM decimator delay is not reported)
    arr::SourceRegistration arcsSource;               // ARRANGE can re-render ARCS clips (from the page's render blob)
    arr::SourceRegistration labSource;                // lets ARRANGE re-render clips that came from the SPECTRAL LAB
    static arr::PayloadPtr toPayload(const lab::LabHost::ExportSound& e);   // a Lab render -> an ARRANGE clip source
    void publish();
    int addLayer(int kind, const juce::String& name, const juce::String& words, uint32_t seed, std::shared_ptr<gs::SampleData> sample = nullptr);
    int layerFor(int kind, const juce::String& name);                // existing layer with that sound, or a new one (-1 if full)
    void removeLayer(int i);
    void audition(int layer, int note = 60) { engine.auditionNote.store(note); engine.auditionLayer.store(layer); }
    void auditionDrum(const juce::String& name) { if (auto s = drums->byName(name)) engine.auditionDrum.store(s.get()); }
    juce::ValueTree toTree() const;
    void fromTree(const juce::ValueTree& t);
    juce::File userDir() const;
    bool saveCanvas(const juce::String& name);
    bool loadCanvas(const juce::File& f);
    juce::File bounce(juce::Component* ui = nullptr);              // DRAG PATTERN OUT (WAV): gate "spec.pattern.wav-drag"
    juce::StringArray bounceStems(juce::Component* ui = nullptr);  // DRAG STEMS OUT: gate "spec.pattern.stems-drag"
    // Export gate - every drag-out / file export in every page asks here with its id BEFORE writing anything
    // (ghosty::exportAllowed: demo = refused, the UNLOCK hint shows on ui's editor). exportOpen() = the silent check
    // (paint, timers, background pre-renders).
    bool allowExport(juce::Component* ui, const char* id) { const bool ok = ghosty::exportAllowed(license, ui, id, exportLockForTest.load() == 1); if (!ok) exportRefusals.fetch_add(1); return ok; }
    bool exportOpen() const { return exportLockForTest.load() != 1 && ghosty::exportUnlocked(license); }
    std::atomic<int> exportLockForTest { -1 }, exportRefusals { 0 };   // tests: 1 = demo-locked, else the real licence (0 / -1)
    double bpm() const { return apvts.getRawParameterValue("bpm")->load(); }
    // the tempo everything uses: the host project's tempo whenever the host has ever reported one (even when stopped)
    double hostBpm() const { return lastHostBpm.load(); }
    double tempo() const { if (ownClock()) return sync.getOwnBpm(); const double h = lastHostBpm.load(); return h > 0 ? h : bpm(); }
    // ---- SYNC: DAW | OWN (Shared/SyncTransport.h). DAW (default) = the host's play / tempo / position, exactly as always. OWN (or no host
    // position at all, e.g. the standalone app) = Spectrum's own clock and tempo; the DAW's transport is ignored. Saved per project ("SPECTRUMUI").
    ghosty::SyncTransport sync;
    bool ownClock() const { return ownUi.load(); }                     // the own clock is what drives the players now (UI: show BPM box + PLAY)
    bool ownWanted() const { return sync.getMode() == ghosty::SyncTransport::Mode::Own || wrapperType == wrapperType_Standalone || noHostUi.load(); }
    void toggleOwnPlay() { sync.toggleOwn(0.0); }                      // own PLAY / SPACE: from bar 1, on the grid
    std::atomic<bool> ownUi { false }, noHostUi { false }, hostPlayingUi { false };   // hostPlayingUi: the DAW's play state (DAW mode)
    std::atomic<double> lastHostBpm { 0.0 };

    // ---- PAD painter
    void syncPads();                                  // model.pads -> the engine's dot slots (cheap: call while dragging)
    sp::PadParams padParams() const;                  // knobs + key, any thread
    float padLevel(const sp::PadDot& d) const { return d.slot >= 0 && d.slot < sp::kMaxDots ? engine.padSlots[d.slot].level.load() : 0.0f; }
    // ---- patterns A..H
    void switchPattern(int i);
    int copyPatternToNextFree();                      // returns the new pattern index or -1
    // ---- DETECT KEY
    void startKeyListen();
    int pollKeyListen(juce::String& message, float& progress);   // 0 still listening, 1 key set, -1 heard nothing
    bool keyListening() const { return keyL.active; }
    std::vector<std::shared_ptr<gs::SampleData>> userSamples;
    bool wasHostPlaying = false, wasLabOpen = false, bypassStopPending = false;
    ghosty::themes::Theme uiLook = ghosty::themes::Theme::Dark;   // BLACK / PURPLE / LIGHT (UI preference, saved with the session, never in presets)
    std::atomic<int> guideSeen { 0 };                 // HOW IT WORKS: bit per tab = its first-run hint was seen (saved with the session, UI only)
    // PLAY: ON NOTES gate (audio thread)
    ghosty::BypassFader notesGate; std::bitset<128> heldNotes; int heldCount = 0; bool lastGateHostPlay = false; juce::AudioBuffer<float> gateBuf, gateGain;
    static constexpr int kMaxGate = 64; int gateAt[kMaxGate] {}; bool gateClosed[kMaxGate] {}; int nGate = 0;                               // DARK / LIGHT look (UI preference, saved with the session, never in presets)
    // SYNC switching: the transport-driven players fade out (~6 ms) on the old clock, swap, then fade in on the new one
    bool appliedOwn = false, syncFresh = true; float syncG = 1.0f; int syncDir = 0; double ownSec = 0, syncSr = 48000.0;
    struct OwnHead : juce::AudioPlayHead { PositionInfo pos; juce::Optional<PositionInfo> getPosition() const override { return pos; } } ownHead;
    juce::MidiBuffer pageMidi;                        // the incoming MIDI for ARCS (copied before the lab / pad take theirs)
    juce::MemoryBlock defaultArrange; juce::ValueTree defaultArcs;
    juce::ValueTree plabTree; juce::CriticalSection plabLock; juce::ValueTree defaultVoice;   // what a session without those parts loads
private:
    static BusesProperties makeBuses();
    juce::File renderTo(const sp::SpecSnapshot* s, const juce::File& f, bool withPads = true);
public:
    struct LoopRender { struct Dot { uint32_t id = 0; float pitch = 60, tone = 0.3f, gain = 0.8f; int type = 0; };
        std::shared_ptr<const sp::SpecSnapshot> snap; std::array<std::shared_ptr<gs::SampleData>, sp::kLayers> samples {}; std::array<Dot, sp::kMaxDots> dots {};
        bool anyPad = false; sp::PadParams pp; double bpm = 120; float volume = 0.8f; };
    LoopRender prepareLoopRender(const sp::SpecSnapshot* s, bool withPads) const;   // message thread
    static juce::AudioBuffer<float> renderLoop(const LoopRender& r);                // any thread
    void sendPatternToArrange(std::function<void(juce::uint32)> done = {});        // PAINT PADS / LOOP -> ARRANGE (worker render)
    void sendArcsToArrange(std::function<void(juce::uint32)> done = {});           // ARCS page -> ARRANGE (worker render, notes as MIDI, re-renderable)
    static arr::PayloadPtr arcsPayload(const arr::SourceRef& r);                 // ARCS render blob -> clip sound (any thread)
    std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);
private:
    uint32_t nextPadId = 1;
    std::atomic<float>* padP[11] {};          // the pad knobs' raw values (no string lookups on the audio thread)
    std::atomic<int> padRoot { 0 }, padScale { 1 };
    bool held[128] {}; float lastTranspose = 0;       // audio thread: MIDI keys playing the pad
    struct KeyListen { bool active = false; int inRead = 0, outRead = 0; juce::int64 startMs = 0; double consumed = 0; sp::KeyDetector inDet, outDet; std::vector<float> tmp = std::vector<float>(sp::SpecEngine::kRing); } keyL;
    std::vector<std::unique_ptr<sp::SpecSnapshot>> snaps;
    std::vector<std::shared_ptr<const gs::SampleData>> keep;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpecProcessor)
};
