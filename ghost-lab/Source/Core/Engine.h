#pragma once
// The session: the specimens, the undo history, the experiment-table chain and the worker thread that runs CDP.
// The message thread owns everything here; the worker only renders and hands results back. The audio thread never
// touches this class (it plays what the processor's Player was given).
#include "Actions.h"
#include "Rooms.h"
#include <juce_data_structures/juce_data_structures.h>
#include <deque>
#include <functional>

namespace glab
{
struct Snapshot
{
    AudioPtr audio;          // the processed sound at this point (read-only, shared)
    Profile profile;         // what the body / waveform draw
    juce::String label;      // "PULL 2.5x"
    juce::var recipe;        // the step(s) that made it
    juce::String commands;   // the CDP commands that ran
    // the original (SPECIMEN 001) in force at this step: trimming the original changes it, and undo / redo bring the one
    // that belongs to the step back (build 16)
    AudioPtr source;
    std::shared_ptr<const Profile> sourceProfile;
};

struct ChainSlot
{
    Op op;
    bool on;
    Step step;               // its settings
};

class Engine
{
public:
    Engine();
    ~Engine();

    std::unique_ptr<Cdp> cdp;   // set up in the constructor; replaceable by tests

    // ---- specimens
    bool loadSpecimen (const juce::File& f, juce::String& error);            // SPECIMEN 001 (starts a new history)
    bool loadPartner (const juce::File& f, juce::String& error);             // SPECIMEN 002 (for VOICE FUSION / breeding)
    void setSpecimen (AudioPtr a, const juce::String& name);
    void setPartner (AudioPtr a, const juce::String& name);
    void clearPartner();
    bool hasSpecimen() const { return original != nullptr; }
    bool hasPartner() const { return partner != nullptr; }
    AudioPtr originalAudio() const { return original; }
    AudioPtr partnerAudio() const { return partner; }
    const Profile& originalProfile() const { return origProfile; }
    const Profile& partnerProfile() const { return partProfile; }
    juce::String specimenName, partnerName;
    int specimenNumber = 0;      // the pill: SPECIMEN 001, 002 ...

    // ---- history (every action is one step; undo restores bit-exact)
    const Snapshot& current() const { return history[(size_t) cursor]; }
    const Snapshot& snapshot (int i) const { return history[(size_t) juce::jlimit (0, (int) history.size() - 1, i)]; }
    AudioPtr processedAudio() const { return hasSpecimen() ? current().audio : nullptr; }
    bool canUndo() const { return cursor > 0; }
    bool canRedo() const { return cursor + 1 < (int) history.size(); }
    bool undo();
    bool redo();
    void backToOriginal();       // a new step that is the original again (undoable)
    // ---- trimming (build 16; from / to are 0..1 of the sound's length; both are one undoable step)
    // the original: the part kept becomes the new original that everything works on; the processed sound starts again
    // from it (undo brings back the whole original and the result you had)
    bool trimOriginal (float from, float to, juce::String& error);
    bool trimProcessed (float from, float to, juce::String& error);   // the result only
    static constexpr double kMinTrimSeconds = 0.05;
    int historySize() const { return (int) history.size(); }
    int historyCursor() const { return cursor; }
    static constexpr int kMaxHistory = 40;

    // ---- processing (async: done runs on the message thread; sync versions for tests and the CLI)
    using Done = std::function<void (bool ok, const juce::String& message)>;
    void apply (const Step& s, Done done);                 // on the processed sound
    // the same, but the new result takes the place of the last step (TERRAIN re-renders as the landscape changes: one
    // history entry, rendered from the sound underneath it)
    void applyReplacingLast (const Step& s, Done done);
    AudioPtr audioUnderLast() const;                        // the sound the last step was made from (the original if none)
    void runChain (juce::uint32 seed, Done done);          // GENERATE MUTATION: the experiment-table chain, mutated
    void breed (float lean, Done done);                    // SPECIMEN 001 x SPECIMEN 002
    // GROW (Rooms.h): a new sound out of nothing. asPartner: it becomes SPECIMEN 002; otherwise a new SPECIMEN 001
    // (a new history, like dropping a file)
    void grow (const Step& s, bool asPartner, Done done);
    bool growNow (const Step& s, bool asPartner, juce::String& error);
    bool applyNow (const Step& s, juce::String& error);
    bool runChainNow (juce::uint32 seed, juce::String& error);
    bool breedNow (float lean, juce::String& error);
    bool busy() const { return working.load(); }
    void cancel() { cancelFlag = true; }
    juce::String lastCommands;   // the CDP commands of the last render (DEEP view)

    // ---- the experiment table chain
    std::vector<ChainSlot> chain;
    void moveSlot (int from, int to);
    juce::var chainToVar() const;
    bool chainFromVar (const juce::var& v);
    static juce::File chainsFolder();
    bool saveChain (const juce::String& name, juce::String& error) const;
    bool loadChain (const juce::File& f, juce::String& error);
    juce::StringArray chainSteps (juce::uint32 seed) const;   // for tests: what GENERATE would run

    // ---- persistence (the plug-in state): the sounds live in a session folder as 32-bit float WAVs
    juce::String sessionId;
    juce::File sessionFolder() const;
    juce::ValueTree saveState() const;
    void restoreState (const juce::ValueTree& t);

    std::function<void()> onChange;   // history / specimens changed (message thread)

    // ---- the result as a file to drag into FL (24-bit WAV), or saved
    juce::File exportProcessed (juce::String& error, const juce::File& where = {}) const;
    static juce::File exportsFolder();

private:
    AudioPtr original, partner;
    Profile origProfile, partProfile;
    std::vector<Snapshot> history;
    int cursor = 0;
    std::atomic<bool> working { false }, cancelFlag { false };

    struct Worker;
    std::unique_ptr<Worker> worker;

    std::vector<Step> mutatedChain (juce::uint32 seed) const;
    void push (Snapshot s);
    void followSource();         // after undo / redo: the original that belongs to the step now showing
    void persist (const AudioPtr& a, const juce::String& name) const;
    void runAsync (std::function<juce::String (Snapshot&)> job, Done done, bool replaceLast = false);
    juce::String renderChain (const std::vector<Step>& steps, AudioPtr start, AudioPtr partnerNow, Snapshot& out);
};
}
