#pragma once
#include "MickensEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <future>

class MickensProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    MickensProcessor();
    ~MickensProcessor() override;
    const juce::String getName() const override { return "Mr. Mickens"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Mr. Mickens"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- message-thread model
    juce::AudioProcessorValueTreeState apvts;
    mk::Engine engine;
    // the break list, in menu order: your Breaks folder (real records you own), the 100 built-in re-creations,
    // then breaks dropped onto a bay (those are saved inside the session). Bays remember breaks by key, not index.
    std::vector<mk::BreakPtr> folderBreaks;
    std::shared_ptr<const mk::BreakLibrary> lib;   // built once per app in the background, shared by every instance
    std::vector<mk::BreakPtr> userBreaks;
    std::atomic<int> listVersion { 0 };           // bumps whenever the list changes (editor rebuilds its menus)
    std::array<juce::String, mk::kBays> bayKey { "Amen|Amen - Original", "Think|Think - Original", "Two Step|Two Step - Ghosts", "Apache|Apache - Original" };
    static juce::String keyOf(const mk::Break& b) { return b.family + "|" + b.name; }
    juce::File breaksFolder() const { auto d = saveDir().getChildFile("Breaks"); d.createDirectory(); return d; }
    void rescanFolder();                          // background scan of the Breaks folder
    bool libReady() const { return lib != nullptr; }
    void pickBreak(int bay, int index) { if (auto br = breakAt(index)) { bayKey[(size_t) bay] = keyOf(*br); bayBreak[(size_t) bay] = index; publish(); } }
    gb::DrumLibrary drums;
    std::array<int, mk::kBays> bayBreak { -1, -1, -1, -1 };   // index into the list (from bayKey; -1 = not loaded yet)
    std::array<std::array<uint8_t, mk::kSteps>, mk::kDrums> pattern {};
    std::array<juce::String, mk::kDrums> drumName { "Hard Kick", "Big Snare", "Tight Hat", "Short Open Hat" };
    std::array<gb::SoundPtr, mk::kDrums> userDrum {};   // a sample dropped on a drum lane

    int numBuiltIn() const { return lib ? (int) lib->breaks().size() : 0; }
    int numBreaks() const { return (int) folderBreaks.size() + numBuiltIn() + (int) userBreaks.size(); }
    mk::BreakPtr breakAt(int i) const {
        if (i < 0) return nullptr;
        if (i < (int) folderBreaks.size()) return folderBreaks[(size_t) i]; i -= (int) folderBreaks.size();
        if (i < numBuiltIn()) return lib->breaks()[(size_t) i]; i -= numBuiltIn();
        return i < (int) userBreaks.size() ? userBreaks[(size_t) i] : nullptr; }
    void resolveBays() { for (int b = 0; b < mk::kBays; ++b) { int found = -1; for (int i = 0; i < numBreaks() && found < 0; ++i) if (keyOf(*breakAt(i)) == bayKey[(size_t) b]) found = i; bayBreak[(size_t) b] = found; } ++listVersion; publish(); }
    gb::SoundPtr drumSound(int d) const { return userDrum[(size_t) d] ? userDrum[(size_t) d] : drums.byName(drumName[(size_t) d]); }
    static juce::StringArray drumChoices(int d);

    void publish();
    void setPlaying(bool on) { if (on && !engine.playing.load()) engine.restart(); engine.playing.store(on); }
    bool addUserBreak(const juce::File& f, int bay, juce::String& err);
    bool setUserDrum(int d, const juce::File& f, juce::String& err);
    void defaultPattern(int style);   // 0 two-step, 1 halftime, 2 roller, 3 empty
    void diceStarts();                // the DICE button: new start for every bay (on its grid)

    juce::File saveDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Mr Mickens"); d.createDirectory(); return d; }
    bool saveSession(const juce::String& name) { juce::MemoryBlock mb; getStateInformation(mb); return saveDir().getChildFile(juce::File::createLegalFileName(name) + ".mickens").replaceWithData(mb.getData(), mb.getSize()); }
    bool loadSession(const juce::File& f) { juce::MemoryBlock mb; if (!f.loadFileAsData(mb) || mb.getSize() == 0) return false; setStateInformation(mb.getData(), (int) mb.getSize()); return true; }
    juce::File bounce(int bars);                 // whole mix to WAV (drag out)
    juce::StringArray bounceStems(int bars);     // breaks / kick / snare / hats
    double currentBpm() const { return lastBpm.load(); }
    std::atomic<double> lastBpm { 172 };
    std::atomic<bool> hostRunning { false };
    bool darkTheme = false;   // UI look, saved with the session

private:
    void timerCallback() override;
    std::future<std::vector<mk::BreakPtr>> folderScan;
    void readParams(std::array<mk::BayParams, mk::kBays>& B, mk::FxParams& F) const;
    juce::File renderTo(uint32_t mask, int bars, const juce::File& f);
    std::vector<std::unique_ptr<mk::Snapshot>> snaps;
    std::vector<mk::BreakPtr> keepBreaks; std::vector<gb::SoundPtr> keepSounds;
    struct BayP { std::atomic<float> *nudge, *grid, *pitch, *level, *decay, *pan, *rs, *rc, *ra, *dk, *ds, *dh, *mute, *fit; } bp[mk::kBays] {};
    std::atomic<float>* fx[40] {};
    juce::AudioBuffer<float> chunk; juce::MidiBuffer subMidi;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MickensProcessor)
};
