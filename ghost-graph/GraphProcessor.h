// Ghost Graph -- music written as equations. Each scene is a few lines of math; every voice in its y list is a
// tone whose pitch (in octaves) is that equation at the time x (in beats). The window draws the graphs as they play.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "GraphExpr.h"
#include "GraphTracks.h"
#include "GraphGen.h"
#include "GraphFamous.h"

class GraphProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxScenes = 12;
    static constexpr int kPage = 16;          // one scene page = 16 beats (x from -8 to 8)

    GraphProcessor();
    ~GraphProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghost Graph"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    // ---- scenes (message thread)
    int getNumScenes() const { return (int) scenes.size(); }
    const gg::Scene& getScene (int i) const { return scenes[(size_t) juce::jlimit (0, (int) scenes.size() - 1, i)]; }
    void setSceneText (int i, const std::string& text);   // recompiles; the sound switches over at once
    int addScene();                                       // returns its index (or -1 when full)
    void removeScene (int i);
    void resetScenes();                                   // the six factory scenes
    void setSceneSpeed (int i, double s) { if (i >= 0 && i < (int) scenes.size()) { scenes[(size_t) i].speed = s; rebuildAll(); } }
    void loadTrack (int i);                               // a whole reference video: its scenes and its tempo
    void generateTrack (uint32_t seed, int style = -1);   // GENERATE: a brand-new song of our own, in one of the videos' styles
    void generateSceneHere (int i);                       // a new scene in this slot, same song as the last GENERATE
    // EXPERIMENT: a song from a famous equation (which: from gg::Experiment::list(), -1 any; wild: 1 raw, 0 musical, -1 decide)
    void generateExperiment (uint32_t seed, int which = -1, int wild = -1);
    std::unique_ptr<gg::Experiment> lastExperiment;
    bool canUndoGenerate() const { return ! beforeGenerate.empty(); }
    void undoGenerate();
    std::atomic<int> scenesVersion { 0 };                 // bumps when scenes are added / removed / loaded

    // ---- what the window shows (written by the audio thread)
    std::atomic<double> displayX { -8.0 };
    std::atomic<int> displayScene { 0 };
    std::atomic<bool> displayRunning { false };
    std::atomic<bool> displayHostSync { false };          // true when the host's transport drives the clock
    std::atomic<double> displayBpm { 75.0 };              // the tempo it is playing at (the DAW's, when SYNC follows it)
    std::atomic<bool> displaySeenHost { false };          // a DAW has told it a tempo

    // ---- DRAG: render the song (or some of its voices) on its own, faster than real time
    // A copy of this plugin, made on the message thread, that can render on another thread.
    std::unique_ptr<GraphProcessor> makeRenderCopy();
    // On a render copy: the whole song from the top at bpm, only the voices in mask (one bit per voice, per scene;
    // empty = all of them), through TONE / SPACE / VOLUME like the real thing. Stops early if cancel turns true.
    void renderOffline (juce::AudioBuffer<float>& out, double bpm, double seconds, double sampleRate,
                        const std::vector<uint16_t>& mask, const std::atomic<bool>* cancel = nullptr);

    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }

    // test hook: render with a fake host transport
    struct FakeHost { bool use = false, playing = false; double ppq = 0, bpm = 120; };
    FakeHost fakeHost;

private:
    std::vector<gg::Scene> scenes;
    std::vector<gg::Scene> beforeGenerate;
    float tempoBeforeGenerate = 75.f;
    std::unique_ptr<gg::Generator> gen;
    std::array<std::unique_ptr<gg::Program>, kMaxScenes> programs;
    std::atomic<int> numPrograms { 0 };
    std::array<double, kMaxScenes> speeds {}, pitches {};
    juce::SpinLock progLock;
    void rebuildAll();
    static std::string sceneName (int i);

    // clock
    double sr = 48000.0;
    double freeBeat = 0.0;        // internal clock (SYNC off / standalone)
    std::atomic<bool> restartClock { false };
    double keyBeat = 0.0;         // runs while keys are held and nothing else is playing
    bool keyClockOn = false;

    // keys
    std::array<int, 16> held {};
    int numHeld = 0;
    int lastNote = 69;

    struct Voice
    {
        double phase = 0, y = 0, yFrom = 0, yTo = 0, gain = 0;
        double hatPh[6] {}, hp1 = 0, hp2 = 0, hpx1 = 0, hpx2 = 0, lp1 = 0, lp2 = 0;   // the hi-hat's six metal oscillators + its high-pass
        uint32_t noise = 0x9E3779B9u;
        bool on = false;
    };
    std::array<Voice, gg::Program::kMaxVoices> voices;
    std::array<uint16_t, kMaxScenes> soloMask {};   // DRAG renders: which voices sound (all of them normally)
    double offlineBpm = 0;                          // DRAG renders: the tempo, whatever the clock says
    int lastSceneIdx = -1;

    juce::dsp::StateVariableTPTFilter<float> lowpass;
    juce::Reverb reverb;
    juce::SmoothedValue<float> volSmooth, toneSmooth;

    std::atomic<float>* pSync; std::atomic<float>* pPlay; std::atomic<float>* pTempo; std::atomic<float>* pRate;
    std::atomic<float>* pChain; std::atomic<float>* pScene; std::atomic<float>* pWave; std::atomic<float>* pTune;
    std::atomic<float>* pGlide; std::atomic<float>* pTone; std::atomic<float>* pSpace; std::atomic<float>* pWidth;
    std::atomic<float>* pVolume; std::atomic<float>* pKeys; std::atomic<float>* pKit;

    void handleMidi (const juce::MidiMessage& m);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphProcessor)
};
