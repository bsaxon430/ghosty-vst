#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Engine.h"
#include "Player.h"
#include "TerrainInteraction.h"
#include "TerrainDsp.h"
#include "../Core/LabTerrainMap.h"
#include "Space.h"   // (Source/Matter/Engine: the SPACE room's engine glue)
#include "../Core/Spectre.h"

namespace glab
{
// GHOSTLAB_FX=1 builds "Ghost Lab FX": an audio effect (FL generators get no audio input, so this is how a track's
// audio reaches Ghost Lab: CAPTURE records what plays through it). Otherwise the instrument + the standalone app.
class Processor : public juce::AudioProcessor, private juce::Timer
{
public:
    Processor();
    ~Processor() override;

    static constexpr bool isFx =
       #if GHOSTLAB_FX
        true;
       #else
        false;
       #endif

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return ! isFx; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    Engine engine;
    Player player;
    void syncPlayer();          // message thread: hand the current original / processed to the player
    void refreshState();        // message thread: the cached state the host reads

    // ---- FX capture (records the input; max 60 s)
    void startCapture();
    AudioPtr stopCapture();     // message thread
    bool capturing() const { return captureOn.load(); }
    double capturedSeconds() const { return captureRate > 0 ? captureLen.load() / captureRate : 0.0; }
    static constexpr double kCaptureSeconds = 60.0;

    std::atomic<float> previewGain { 0.9f };

    // the editor's size while this instance lives (closing and re-opening the window keeps it); a new instance always
    // opens at the fitting size (Editor::kW x kH), never at a size from another instance or project
    juce::Point<int> editorSize;

    // ---- TERRAIN (the landscape page): the state lives here so it survives the editor closing and is saved with the session
    ghost::terrain::TerrainEditor terrain;              // message thread: the landscape, A/B, undo
    ghost::terrain::SpectrumTap terrainTap;             // the analyzer's feed (audio -> message)
    std::function<void()> onTerrainCommit;              // the editor renders when an undoable edit lands
    // LISTEN: the Player's Preview slot plays the sound underneath the landscape through the live resonator bank,
    // the bank following the landscape as it is dragged (no locks: TerrainShare) and the playhead along the time axis
    void setTerrainListening (bool on);                 // message thread
    bool terrainListening() const { return player.playingSlot() == Player::Preview; }
    float terrainPlayhead() const { return terrainListening() ? player.playhead() : -1.0f; }
    AudioPtr terrainBase() const;                       // the sound the landscape is applied to
    // build 4: the land drives the lab's processes (LabTerrainMap.h). While you drag, the live path plays (the bank + the live
    // chain, every move heard at the next block); when the exact render of the land lands it cross-fades in (150 ms), never
    // stopping. A drag on the page starts the listen when nothing plays.
    void setTerrainPage (bool open) { terrainPageOpen = open; }
    void setExactPreview (AudioPtr rendered);          // message thread: the exact render of the land now on the page
    bool exactAudible() const { return exactShown.load() > 0.5f; }
    const LabLand& labLand() const { return landNow; }
    float liveLatencyMs() const { return getSampleRate() > 0 ? 1000.0f * (float) previewBlock / (float) getSampleRate() : 0.0f; }

    // ---- THE LIVING SOUND, live (build 11): while a hand is on the body and moving it, the processed sound plays
    // round and round (the Player's Touch slot) through a second live chain - the same kind the TERRAIN page drives -
    // set from how the body is being pulled / pinched / twisted / shaken, so the sound changes as the hand moves
    // (heard at the next audio block). It is a sketch of what each gesture's real process will do; letting go runs the
    // real one, and when that lands the real result plays. Message thread -> audio thread without locks (LiveShare).
    struct BodyTouch { float pull = 0, pinch = 0, twist = 0, shake = 0, bend = 0; };
    static ghost::terrain::LiveParams bodyLive (const BodyTouch& b);   // what the live chain does for a touch (tests)
    void beginBodyTouch (float fromFraction);   // the hand has moved: the loop starts where it grabbed (or carries on from what was playing)
    void setBodyTouch (const BodyTouch& b);     // how the body is held right now
    void endBodyTouch();                        // the hand is off and no process follows: back to what played before (or silence)
    void forgetBodyTouch();                     // the user chose something else to listen to: let go, leave the playback alone
    void bodyResultLanded();                    // the gesture's real render is here: the live chain lets go, the new sound plays from its start
    bool bodyTouching() const { return touchOn; }
    const ghost::terrain::LiveParams& bodyLiveNow() const { return touchParams; }

    // ---- the SPACE room, live (build 12): LISTEN plays the sound round and round through the space engine, which follows
    // the pad and the controls as they move (heard at the next block). PRINT IT renders the same thing into the history.
    void setSpaceListening (bool on, AudioPtr base = nullptr);   // message thread
    bool spaceListening() const { return player.playingSlot() == Player::Room; }
    void setSpaceLive (const float* values17)                    // message thread -> audio thread (plain atomics): the two modules' controls
    {
        for (int i = 0; i < rooms::kSpaceControls; ++i) spaceV[i].store (values17[i]);
    }
    float spaceMeter (int ch) const { return spaceLevel[juce::jlimit (0, 1, ch)].load(); }   // what each ear is getting (0..1)

    // ---- SPECTRE (build 17): the resynthesis room, live. Its controls live here (saved with the session); the page and
    // the audio thread share them through plain atomics.
    //   Ghost Lab FX: LIVE runs the track's own audio through SPECTRE as it plays (the plug-in then reports SPECTRE's
    //   latency so the host lines it up). Ghost Lab (the instrument): LISTEN plays your sound round and round through it.
    // Every control is a plug-in parameter too ("SPECTRE PHASE" ...), so FL can automate it.
    void setSpectreParam (int p, float v);                     // message thread (tells the host)
    float spectreParam (int p) const { return spectreP[(size_t) juce::jlimit (0, spectre::kParams - 1, p)]->get(); }
    void beginSpectreGesture (int p);                          // a hand is on the control (FL records automation between these)
    void endSpectreGesture (int p);
    juce::AudioParameterFloat* spectreParameter (int p) const { return spectreP[(size_t) juce::jlimit (0, spectre::kParams - 1, p)]; }
    spectre::Params spectreParams() const;
    void setSpectreLive (bool on);                              // message thread (FX only)
    bool spectreLiveOn() const { return isFx && spectreOn.load(); }
    void setSpectreListening (bool on, AudioPtr base = nullptr);   // message thread
    bool spectreListening() const { return player.playingSlot() == Player::Room && roomEngine.load() == 1; }
    // the WAVETABLE the partials play: a built-in one by name, or one cycle from a file (empty cycle: plain sines)
    void setSpectreTable (const juce::String& name, const std::vector<float>& cycle);   // message thread
    const juce::String& spectreTableName() const { return tableName; }
    const std::vector<float>& spectreTable() const { return tableCycle; }
    // what the page draws: the engine the sound is going through right now (the listen loop if it plays, else the track)
    const spectre::Engine& spectreShown() const { return spectreListening() || ! spectreLiveOn() ? spectreLoop : spectreTrack; }

private:
    spectre::Engine spectreTrack, spectreLoop;                  // audio thread only (made in prepareToPlay)
    std::array<juce::AudioParameterFloat*, spectre::kParams> spectreP {};
    std::array<float, spectre::kParams> spectreSaved {};       // message thread: the values last written into the state
    juce::String tableName { "SINE" };
    std::vector<float> tableCycle;
    std::atomic<bool> spectreOn { isFx }, spectreDirty { false };
    std::atomic<int> roomEngine { 0 };                          // what the Room slot's loop goes through: 0 SPACE, 1 SPECTRE
    bool spectreWasLive = false;                                // audio thread
    juce::AudioBuffer<float> monoBuf;
    matter::Space liveSpace;                            // audio thread only (made in prepareToPlay)
    std::atomic<float> spaceV[rooms::kSpaceControls] {};
    std::atomic<float> spaceLevel[2] { 0.0f, 0.0f };
    bool spaceWasLive = false;                          // audio thread
    int spaceBlock = 256;
    static BusesProperties buses();
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    void timerCallback() override;
    juce::CriticalSection stateLock, captureLock;   // never taken on the audio thread
    juce::MemoryBlock cachedState;
    juce::AudioBuffer<float> captureBuf, previewBuf;
    ghost::terrain::TerrainShare terrainShare;          // message -> audio
    ghost::terrain::TerrainBank terrainBank;            // audio thread only
    ghost::terrain::LiveShare liveShare;                // message -> audio
    ghost::terrain::TerrainLive terrainLive;            // audio thread only
    LabLand landNow;                                    // message thread
    ghost::terrain::LiveShare touchShare;               // message -> audio: the body's live chain settings
    ghost::terrain::TerrainLive touchLive;              // audio thread only
    ghost::terrain::LiveParams touchParams;             // message thread: the last ones sent
    bool touchOn = false, touchWasLive = false;         // message thread; audio thread
    int touchResume = -1;                               // message thread: what was playing when the hand landed
    bool terrainPageOpen = false, autoListen = false;
    void applyLand();
    // the exact render, read at the live path's place (the audio thread holds `exactInUse` while it reads)
    std::atomic<const Audio*> exactPtr { nullptr }, exactInUse { nullptr };
    std::atomic<int> exactBaseLen { 1 };
    std::vector<AudioPtr> exactOwners;                  // message thread: kept until the audio thread has let go
    std::atomic<float> exactShown { 0.0f };
    float exactMix = 0.0f;                              // audio thread
    juce::AudioBuffer<float> exactBuf;
    int previewBlock = 512;
    void renderPlayer (juce::AudioBuffer<float>& dst, juce::MidiBuffer& midi, int n);
    double captureRate = 44100.0;
    std::atomic<bool> captureOn { false };
    std::atomic<int> captureLen { 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
}
