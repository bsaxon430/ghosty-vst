#pragma once
// GHOST - the song machine. One plug-in that opens the owner's own plug-ins inside itself and wires them up:
//
//   ArpEZator (writes the song) --MIDI ch 3 CHORDS / 1 ARP / 2 MELODY / 4 BASS--> 4 LANES
//   each LANE = one sound from Ghosty Pro 2, Poltergeist 2 or Ghost Atlas (the real plug-in) -> TONE -> SPECTRAL FX -> level
//   Ghosty Beats (the drums: QUICK page, sequencer, drum sounds)                                -> level
//   everything lined up in time (the FX's delay is made up on the other parts) -> master -> out
//
// The DAW's MIDI keyboard plays the selected lane (to try a sound). When the DAW is stopped, GHOST's PLAY runs a clock of
// its own that ArpEZator and Ghosty Beats both follow, so they always play together.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Catalog.h"
#include "GraphSong.h"
#include "ghostblock/spectral/spectral-fx.h"
#include <array>
#include <atomic>

namespace ghost
{
namespace sfx = ghostblock::p2::spectral;

constexpr int kLanes = 4;
enum LaneId { LaneChords = 0, LaneArp, LaneMelody, LaneBass };
const char* laneName (int lane);           // "CHORDS", "ARP", "MELODY", "BASS"
int laneMidiChannel (int lane);            // ArpEZator's MIDI out: CHORDS 3, ARP 1, MELODY 2, BASS 4

// a delay line for lining parts up (made on the message thread, used on the audio thread)
struct AlignDelay
{
    juce::AudioBuffer<float> buf; int w = 0; std::atomic<int> d { 0 };
    void prepare (int maxDelay) { buf.setSize (2, juce::jmax (1, maxDelay + 1)); buf.clear(); w = 0; }
    void process (float* L, float* R, int n) noexcept;
};

class GhostProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GhostProcessor();
    ~GhostProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& l) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "GHOST"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    // ---------------------------------------------------------------- the parts (message thread)
    struct Lane
    {
        juce::SpinLock audioLock;                          // held by the audio thread while it plays this lane
        std::unique_ptr<juce::AudioPluginInstance> plugin; // the synth
        Engine engine = Engine::GhostyPro2;
        bool hasEngine = false;
        juce::String soundId, soundName, soundCategory;    // what is loaded
        bool edited = false;                               // changed in the synth's own editor since it was loaded
        juce::String error;                                // why it's silent ("Ghost Atlas isn't installed")
        sfx::SpectralFx fx;
        int fxLatency = 0, pluginLatency = 0;
        AlignDelay align;
        juce::MidiBuffer midi;                             // this block's notes
        juce::dsp::StateVariableTPTFilter<float> lp, hp;   // TONE
        float lastTone = -2.0f;
        std::atomic<float> meter { 0.0f };
        std::atomic<bool> activity { false };
        std::atomic<int> notesIn { 0 };                    // notes that reached this lane (checks)
    };
    std::array<Lane, kLanes> lanes;
    // loads a sound on a lane (opens the synth if the lane has another one). false + error when it can't.
    bool loadSound (int lane, const juce::String& soundId, juce::String& error);
    bool loadSound (int lane, const Sound& s, juce::String& error);
    // < > and NEXT GOOD on a lane
    juce::String stepSound (int lane, int dir);            // the next / previous sound of the same synth and folder... returns the name or an error
    juce::String nextGood (int lane);                      // a random sound that suits the lane
    std::vector<Sound> suitable (int lane) const;
    static std::vector<Role> rolesFor (int lane);

    // ---------------------------------------------------------------- where the song comes from
    enum Source { FromArpez = 0, FromGraph = 1 };
    std::atomic<int> source { FromArpez };                 // the ARPEZ page plays ArpEZator's song, the GRAPH page Ghost Graph's
    void setSource (int s);
    std::unique_ptr<juce::AudioPluginInstance> graph;      // Ghost Graph (its own sound is not heard: its notes play the lanes)
    juce::SpinLock graphLock;
    juce::String graphError;
    std::atomic<GraphSong*> graphLive { nullptr };         // the song the audio thread plays (swapped on the message thread)
    GraphSong graphShown;                                  // a copy for the window (message thread)
    juce::String beatsKit;                                 // what GHOST read of Ghosty Beats' kit (checks / the window)
    // ArpEZator's lanes as ArpEZator has them: a lane its mood mutes is silent there (and so in GHOST)
    std::array<bool, kLanes> arpMuted {};
    std::array<int, kLanes> arpNoteCount {};
    std::atomic<int> graphDrumHits { 0 };                  // (checks) drum hits sent to Ghosty Beats
    void unmuteArpLane (int lane);                         // ArpEZator plays that lane again (its own MUTE, switched off)

    std::unique_ptr<juce::AudioPluginInstance> arp;        // ArpEZator
    std::unique_ptr<juce::AudioPluginInstance> beats;      // Ghosty Beats
    juce::SpinLock arpLock, beatsLock;
    juce::String arpError, beatsError;
    AlignDelay beatsAlign;
    int beatsLatency = 0, arpLatency = 0;
    std::atomic<float> beatsMeter { 0.0f };
    void openParts();                                      // opens ArpEZator / Ghosty Beats (and the lanes' synths) if they aren't open
    juce::AudioPluginInstance* laneSynth (int lane) { return lanes[(size_t) lane].plugin.get(); }

    // ---------------------------------------------------------------- the FX of a lane (Poltergeist 2's rack)
    void fxChanged (int lane);                             // after an FX edit: build what changed, line things up again

    // ---------------------------------------------------------------- transport
    std::atomic<bool> ownPlay { false };                   // GHOST's PLAY (when the DAW is stopped)
    std::atomic<bool> dawPlaying { false };
    std::atomic<double> shownBpm { 120.0 }, shownPpq { 0.0 };
    void togglePlay();

    std::atomic<int> selectedLane { 0 };                   // the DAW's keyboard plays this lane
    std::atomic<float> masterMeterL { 0.0f }, masterMeterR { 0.0f };
    std::atomic<int> uiVersion { 0 };
    std::function<void (int lane)> onLanePluginClosing;   // (the editor closes that lane's synth window before the synth goes)                      // bumps when a lane's sound / FX / an error changes

    double rate() const { return sr; }
    int block() const { return maxBlock; }
    int totalLatency() const { return alignTo; }

private:
    void timerCallback() override;
    void realign();                                        // message thread: everybody waits for the slowest part
    bool openLanePlugin (int lane, Engine e, juce::String& error);
    void prepareLane (Lane& L);
    void silenceSoundFx (juce::AudioPluginInstance* p);
    void setArpSoundOff();

    struct PlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    } hostedHead, beatsHead;

    double sr = 48000.0; int maxBlock = 512; bool prepared = false;
    double ownPpq = 0.0; bool wasOwn = false;
    int alignTo = 0;
    juce::AudioBuffer<float> scratch, arpScratch;
    juce::MidiBuffer arpMidiIn, arpMidiOut, empty;
    std::vector<juce::String> pendingLoads;                 // (restoring a song before the parts were open)
    juce::ValueTree restoreTree;                            // the song being restored, applied once the parts are open
    void applyRestore();
    bool partsOpened = false;
    int tick = 0;
    std::atomic<bool> needRealign { false };
    // GRAPH
    void refreshGraphSong (bool force);
    void retireSong (GraphSong* s);
    std::vector<std::pair<juce::uint32, std::unique_ptr<GraphSong>>> retired;
    juce::MemoryBlock lastGraphState;
    DrumMap drumMap {};
    juce::uint32 lastKitRead = 0;
    double graphPrevEnd = -1.0;
    bool graphWasPlaying = false;
    std::array<std::array<bool, 128>, kLanes> graphActive {};
    void graphFlush (int at);
    void graphSchedule (const juce::AudioPlayHead::PositionInfo& info, int at, int len, double bpm);
    juce::MidiBuffer graphDrums;
    int lastSource = FromArpez;
    // ArpEZator's lanes
    void pollArp();
    juce::String lastIdeaKey;
    bool ideaKnown = false, restoredSong = false;
    juce::uint32 lastArpPoll = 0;
    bool editArpIdea (const std::function<void (juce::var& idea)>& edit);
    std::atomic<float>* pMaster = nullptr;
    std::array<std::atomic<float>*, kLanes> pLevel {}, pTone {}, pMute {};
    std::atomic<float>* pBeatsLevel = nullptr;
    std::atomic<float>* pBeatsMute = nullptr;
    juce::LinearSmoothedValue<float> gLane[kLanes], gBeats, gMaster;
    juce::dsp::Limiter<float> safety;   // the last thing before the output: GHOST never clips

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GhostProcessor)
};
} // namespace ghost
