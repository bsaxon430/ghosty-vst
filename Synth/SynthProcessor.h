#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Waves.h"
#include "Fx.h"
#include "Performer.h"

class SynthProcessor;

struct GhostSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

namespace ghosty
{
// Linear ADSR with the same curves as juce::ADSR, but its times and sustain can change while a note is held
// (juce::ADSR::setParameters during release recomputes the release rate from the sustain level and can cut the note).
struct LiveEnv
{
    enum class St { idle, attack, decay, sustain, release };
    St st = St::idle; float v = 0, relFrom = 0; double sr = 44100;
    float a = 0.01f, d = 0.1f, s = 1.0f, r = 0.1f;
    void set (float A, float D, float S, float R) noexcept { a = A; d = D; s = juce::jlimit (0.0f, 1.0f, S); r = R; }
    void on() noexcept
    {
        if (a > 0) st = St::attack;
        else if (d > 0 && s < 1) { v = 1; st = St::decay; }
        else { v = s; st = St::sustain; }
    }
    void off() noexcept { if (st == St::idle) return; if (r > 0) { relFrom = v; st = St::release; } else reset(); }
    void reset() noexcept { st = St::idle; v = 0; }
    bool active() const noexcept { return st != St::idle; }
    float next() noexcept
    {
        switch (st)
        {
            case St::idle: return 0;
            case St::attack:  v += (float) (1.0 / (a * sr)); if (v >= 1) { v = 1; st = (d > 0 && s < 1) ? St::decay : St::sustain; } break;
            case St::decay:   v -= (float) ((1.0 - s) / (d * sr)); if (v <= s) { v = s; st = St::sustain; } break;
            case St::sustain: v += (s - v) * 0.002f; break;                 // follows a moved SUSTAIN knob without a jump
            case St::release: v -= (float) (relFrom / (r * sr)); if (v <= 0) { v = 0; st = St::idle; } break;
        }
        return v;
    }
};
}

class GhostVoice : public juce::SynthesiserVoice
{
public:
    explicit GhostVoice (SynthProcessor& p) : proc (p) {}
    void prepare (double sr, int block);
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<GhostSound*> (s) != nullptr; }
    void startNote (int midi, float vel, juce::SynthesiserSound*, int wheel) override;
    void stopNote (float, bool allowTail) override;
    void pitchWheelMoved (int v) override { bend = std::pow (2.0, ((v - 8192) / 8192.0) * 2.0 / 12.0); }
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>&, int start, int num) override;

    // mono mode: move to a new note without restarting oscillators; retrigger = restart envelopes from where they are
    void legatoTo (int midi, float vel, bool retrigger);

private:
    SynthProcessor& proc;
    ghosty::LiveEnv amp, fenv;
    juce::dsp::StateVariableTPTFilter<float> filter;
    int filterType = 0;
    std::array<double, 7> phase {};
    double subPhase = 0, bend = 1;
    double logTarget = 0, logNow = 0;         // log2 of the note frequency (glide moves logNow toward logTarget)
    float velocity = 1, velRaw = 1;
    double sr = 44100;
    // smoothed controls (one-pole) so knob moves never zipper
    float sMorph = 0, sWidth = 0, sCut = 0, sRes = 0, sFDrive = 0, sVol = 1, sPan = 0;
    void snapSmoothers();
};

// juce::Synthesiser plus MONO / LEGATO voice handling, mod wheel capture.
class GhostSynth : public juce::Synthesiser
{
public:
    explicit GhostSynth (SynthProcessor& p) : proc (p) {}
    void setMono (bool shouldBeMono, bool shouldBeLegato);
    void noteOn (int ch, int note, float vel) override;
    void noteOff (int ch, int note, float vel, bool allowTail) override;
    void allNotesOff (int ch, bool allowTail) override;
    void handleController (int ch, int cc, int value) override;
private:
    SynthProcessor& proc;
    bool mono = false, legato = false;
    std::array<int, 32> stack {}; int stackN = 0; int monoNote = -1;
    void removeFromStack (int note);
};

class SynthProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::MidiKeyboardState::Listener,
                       private juce::Timer
{
public:
    SynthProcessor();
    ~SynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghosty"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    static juce::String getPresetCategory (int index);

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- waves. frameA / frameB belong to the MESSAGE thread (the editor draws on them).
    // Other threads (host program change, state load) hand frames over through a locked request that the
    // message thread picks up; only the message thread builds tables, the audio thread just picks up finished sets.
    ghosty::Frame frameA, frameB;
    void rebuildWaves();                  // message thread: frameA/frameB changed, rebuild + publish
    void flushPendingWaves();             // message thread: apply any waiting request / wave-choice change now

    // ---- user sounds ("save everything": all parameters + both drawn frames)
    static juce::File userPresetDir();
    juce::Array<juce::File> listUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    void initPatch();                      // clean saw
    juce::String patchName() const;        // "" = the factory preset getCurrentProgram() names
    bool isUserPatch() const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> level { 0.0f };

    // fast parameter access for the voices
    std::atomic<float>* morph, *unison, *width, *sub, *attack, *decay, *sustain, *release, *cutoff, *res, *fenvAmt, *gain;
    std::atomic<float>* drive, *crush, *chorus, *phaser, *echo, *verb, *haunt;
    std::atomic<float>* keyP, *scaleP, *chordP, *arpP, *arpRateP, *arpModeP, *arpOctP, *arpGateP;
    // added in v2 (defaults keep the 81 factory sounds and old projects sounding the same)
    std::atomic<float>* lfoRateP, *lfoSyncP, *lfoDivP, *lfoShapeP, *lfoDepthP, *lfoDestP;
    std::atomic<float>* glideP, *monoP, *legatoP, *wheelAmtP, *velFiltP;
    std::atomic<float>* fTypeP, *fDriveP, *fLinkP, *fAttackP, *fDecayP, *fSustainP, *fReleaseP;

    juce::MidiKeyboardState keyboardState;     // the on-screen flow strip plays through this
    ghosty::Performer performer;
    bool isNoteSounding (int note) const { return juce::isPositiveAndBelow (note, 128) && heldNotes[(size_t) note].load (std::memory_order_relaxed) != 0; }

    static float cutoffHz (float v) { return 50.0f * std::pow (2.0f, v * 8.5f); }
    static const juce::StringArray& lfoDivNames();
    static const juce::StringArray& lfoShapeNames();
    static const juce::StringArray& lfoDestNames();
    static const juce::StringArray& filterTypeNames();

    // ---- per-block values the voices read (audio thread only)
    struct BlockCtl
    {
        const ghosty::WaveSet* waves = nullptr;
        double lfoPhase = 0, lfoInc = 0;     // LFO phase in cycles at sample 0 of this block, cycles per sample
        int lfoShape = 0, lfoDest = 0; float lfoDepth = 0;
        float wheel = 0, wheelAmt = 0, velFilt = 0, glide = 0;
        double lastNoteLog2 = 0; bool haveLastNote = false;
    } ctl;
    static float lfoValue (int shape, double phaseCycles) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void parameterChanged (const juce::String& id, float) override;
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int ch, int note, float vel) override;
    void handleNoteOff (juce::MidiKeyboardState*, int ch, int note, float vel) override;

    juce::ValueTree makeStateTree();
    void applyStateTree (juce::ValueTree state);
    void setParam (const char* id, float plainValue);
    void resetParamsToDefault (bool soundOnly);

    GhostSynth synth { *this };
    ghosty::FxChain fx;
    juce::MidiBuffer merged, performed;
    static constexpr int midiCapacity = 32768;
    std::atomic<double> sampleRateNow { 44100.0 };
    double builtRate = 0;                       // message thread
    int currentProgram = 0;
    juce::String currentPatchName;              // guarded by reqLock
    bool patchIsUser = false;                   // guarded by reqLock
    float lastGain = -1.0f;
    bool lastMono = false;
    double lfoFree = 0;
    std::array<std::atomic<uint8_t>, 128> heldNotes {};

    // waves: one builder thread (message thread), lock-free hand-over to audio
    ghosty::WaveMailbox waves;
    std::atomic<bool> needA { false }, needB { false }, needRate { false };
    int frameShapeA = 2, frameShapeB = 3;        // message thread: the wave choice each frame was made from
    struct FrameRequest { bool has = false; ghosty::Frame a {}, b {}; int shapeA = 0, shapeB = 0; };
    mutable juce::SpinLock reqLock;                     // never taken on the audio thread
    FrameRequest request;                        // guarded by reqLock
    ghosty::Frame savedA {}, savedB {};          // guarded by reqLock: what getStateInformation writes
    std::atomic<bool> requestPending { false };
    void postFrames (const ghosty::Frame& a, const ghosty::Frame& b, int shapeA, int shapeB);

    // UI notes (flow strip) -> audio thread, lock-free
    struct UiNote { int8_t note; uint8_t vel; bool on; };
    juce::AbstractFifo uiFifo { 256 };
    std::array<UiNote, 256> uiNotes {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthProcessor)
};
