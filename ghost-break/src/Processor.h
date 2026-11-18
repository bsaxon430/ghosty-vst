// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST BREAK host shell: parameters, the pattern (sequences, drum grid, breaks, sounds), undo, state, outputs.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"
#include "Generator.h"
#include <set>

namespace gbk
{
struct SharedLibrary { mk::BreakLibrary breaks; gb::DrumLibrary drums; };
std::shared_ptr<const SharedLibrary> sharedLibrary();   // built once for every instance
inline const char* bayPrefix (int b) { static const char* p[] { "a_", "b_", "c_", "d_" }; return p[juce::jlimit (0, kBays - 1, b)]; }
inline const char* drumPrefix (int d) { static const char* p[] { "k_", "s_", "h_", "o_", "p1_", "p2_" }; return p[juce::jlimit (0, kDrums - 1, d)]; }
juce::String slotPrefix (int slot);          // "a_crunch_", "stut_", "reverb_" ...
juce::String slotTitle (int slot);           // "BAY A CRUNCH (WRECKER)" ...
bool slotParamHidden (int slot, const std::string& id);
bool slotHasOn (int slot);
bool slotHasMix (int slot);

class Processor final : public juce::AudioProcessor, public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ghost Break"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    Engine engine;

    // ---- the pattern (message thread)
    const Pattern& pattern() const noexcept { return cur; }
    void edit (const std::function<void (Pattern&)>& fn, bool undoable = true);
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    void undo();
    void randomStart();
    void randomCombo();
    Rules rules() const; Locks locks() const; std::array<BayP, kBays> bayParams() const; DuckP duckParams() const;
    // breaks, in menu order: CLASSIC BREAKS (the Breaks folder: the real records), the BUILT-IN re-creations, then YOUR BREAKS (the
    // Your Breaks folder and every file added by hand). Files are listed at once and decoded only when a bay picks one.
    enum BreakKind { classicBreak = 0, builtInBreak = 1, yourBreak = 2 };
    struct BreakEntry { juce::String name, section; int libIndex = -1; juce::File file; int kind = builtInBreak; };   // libIndex >= 0: built-in; otherwise a file
    const std::vector<BreakEntry>& breakCatalog() const noexcept { return catalog; }
    int bayBreakIndex (int bay) const;                       // the bay's break in the catalog (-1: not listed)
    void setBayBreak (int bay, int catalogIndex);            // keeps the bay's arrangement; an undo point
    juce::String bayBreakName (int bay) const;
    std::vector<mk::BreakPtr> allBreaks() const;             // the built-in library (what NEW BREAKS picks from, plus loaded files)
    juce::File breaksFolder() const;                         // Music/GhostBlock/Ghost Break/Breaks: the classic breaks
    juce::File yourBreaksFolder() const;                     // Music/GhostBlock/Ghost Break/Your Breaks: breaks you add yourself
    juce::File importBreak (const juce::File&) const;        // a file added by hand: its copy in Your Breaks (or itself)
    void resetBay (int bay);                                 // the bay empty: no break, plain steps, no step locks (an undo point)
    void resetBays();
    void randomDrums();
    double tempoMul() const;                                 // TEMPO: 0.5, 1 or 2
    double playBpm() const;                                  // the tempo the beat plays at (the host's or BPM, times TEMPO)
    // DRAG WAV: the loop as you hear it (one pass of the 32 steps, its tails wrapped in), rendered offline. only: -1 everything, 0..3 one bay
    juce::File renderWav (int only);
    void backHome();                                         // BACK TO MY BEAT: before the RANDOM buttons (an undo point)
    bool canGoHome() const { return homeValid && fromRandom; }
    void backAway();                                         // BACK TO THE BREAK: undo BACK TO MY BEAT (they flip back and forth)
    bool canGoAway() const { return awayValid && ! fromRandom; }                                      // RANDOM DRUMS: a new beat on the unlocked drum lanes (one undo point)                                        // every bay empty (one undo point)
    float uiScale() const;
    bool hasUiScale() const { return apvts.state.hasProperty ("ui_scale"); }                                   // the window's size (1 = 1180 x 760), kept with the project
    void setUiScale (float);
    juce::File drumsFolder() const;
    juce::File kitsFolder() const;                           // Music/GhostBlock/Ghost Break/Classic Break Kits/<break>/<Kicks, Snares, Hi-Hats, Percussion>
    // the drum sound browser: every sound with where it is from and which tab it sits in
    // source: "MICKENS" (the Drums folder), "CLASSIC BREAKS" (the kits folder; kit = the break it was cut from), "BUILT-IN"
    // tab: 0 KICK, 1 SNARE, 2 CLOSED HAT, 3 OPEN HAT, 4 PERCS
    struct DrumEntry { gb::SoundPtr s; juce::String source, kit; int tab = 4; };
    const std::vector<DrumEntry>& drumCatalog() const noexcept { return drumCat; }
    static int kindTab (const juce::String& category) { return category == "Kick" || category == "Sub" ? 0 : category == "Snare" ? 1 : category == "Hat" ? 2 : category == "Open Hat" || category == "Cymbal" ? 3 : 4; }
    void setDrumSound (int drum, const gb::SoundPtr&);       // an undo point                          // Music/GhostBlock/Ghost Break/Drums: Kicks, Snares, Claps, Hi-Hats, Open Hats, Percs sub-folders
    void rescanUserBreaks();                                 // re-lists the Breaks folder
    bool addUserBreak (const juce::File&, int bay, juce::String& err);   // any audio file, into a bay (drag and drop / ADD)
    std::vector<gb::SoundPtr> soundsFor (int drum) const;   // EVERY sound (any lane can hold any): the lane's own category first, yours before the built-in ones
    juce::String laneName (int drum) const;                  // the user's name for the lane, or its starting name
    void setLaneName (int drum, const juce::String&);        // empty = back to the starting name; an undo point
    // a hosted unit's drawn state (WRECKER's DRAW curve, GHOST GATE's patterns ...): message thread; saved with the project.
    // For the DRAW curve use phantom::engines::CurveEngine::varToPoints / pointsToVar / factoryPoints (engines/CurveEngine.h).
    juce::var slotData (int slot) const;
    void setSlotData (int slot, const juce::var&);
    void setDrumSound (int drum, int indexInSoundsFor);      // an undo point
    std::atomic<bool> hostPlayingNow { false }; std::atomic<double> hostBpmNow { 0 };
    juce::String build;

private:
    void timerCallback() override;
    void publish();
    void defaultPattern();
    void writePattern (juce::ValueTree&) const;
    void readPattern (const juce::ValueTree&);
    void pushStutterPad();
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void gather (std::array<BayP, kBays>& B, std::array<DrumP, kDrums>& D, Globals& G) const;
    void feedSlots (Engine& e, bool stutNow) const;
    float raw (const juce::String& id) const { if (auto* p = apvts.getRawParameterValue (id)) return p->load(); return 0.0f; }

    std::shared_ptr<const SharedLibrary> lib;
    std::vector<mk::BreakPtr> userBreaks;   // decoded files (family = full path)
    mutable juce::CriticalSection userLock;
    juce::CriticalSection editLock;          // edits (and UNDO) from more than one thread wait their turn  // RANDOM BREAKS can decode from more than one thread at once (a host's, the window's)
    std::vector<BreakEntry> catalog;
    std::vector<gb::SoundPtr> userDrums;    // the Drums folder's one-shots (listed first in each lane's sound menu)
    void rescanUserDrums();
    std::vector<DrumEntry> drumCat;
    void scanDrumFolder (const juce::File& dir, const juce::String& source, bool kitFromFirstFolder, std::set<juce::String>& names);
    mk::BreakPtr loadFile (const juce::File&, juce::String& err);
    Pattern cur; PatternPtr live;
    struct Retired { PatternPtr p; uint64_t stamp; }; std::vector<Retired> retired;
    struct UndoState { Pattern p; int nudge[kBays]; }; std::vector<UndoState> undoStack;
    // BACK TO MY BEAT: the beat as it was before the RANDOM buttons started (kept until a hand edit makes a new "mine")
    UndoState home {}, away {}; bool homeValid = false, fromRandom = false, randomEditing = false, awayValid = false;
    UndoState snapshot() const; void restore (const UndoState&);
    void beginRandom();
    std::atomic<bool> prepared { false };

    // cached parameter pointers (audio thread reads)
    struct SlotParams { std::vector<std::atomic<float>*> v; std::atomic<float>* on = nullptr; std::atomic<float>* mix = nullptr; };
    std::array<SlotParams, kNumSlots> sp;
    struct BayRaw { std::atomic<float>* nudge, *grid, *pitch, *level, *pan, *decay, *mute, *duck[3], *cho, *rev, *out; } bayRaw[kBays] {};
    struct DrumRaw { std::atomic<float>* level, *pan, *tune, *mute; } drumRaw[kDrums] {};
    std::atomic<float>* gPlay = nullptr, *gBpm = nullptr, *gSwing = nullptr, *gBreaks = nullptr, *gDrums = nullptr, *gVolume = nullptr, *gStutter = nullptr, *gStutLink = nullptr, *gDrumsOut = nullptr;
    std::vector<std::atomic<float>*> duckG; struct DuckRaw { std::atomic<float>* on, *atk, *hold, *rel, *shape, *from[kDrums]; } duckB[kBands] {};
    float lastStepped = -1.0f; int lastStut[3] { -1, -1, -1 }; bool wasPlay = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
} // namespace gbk
