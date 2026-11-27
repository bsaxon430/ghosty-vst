#pragma once
// GHOST - the list of every sound the three synths have: their factory sounds (packed into GHOST, see tools/pack_presets.py)
// plus the owner's own sounds and packs found on this Mac, in the same places the synths themselves look.
// A sound becomes a plug-in state the synth itself understands (stateFor), so loading a sound in GHOST is exactly
// loading it in the synth.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace ghost
{
enum class Engine : int { GhostyPro2 = 0, Poltergeist2 = 1, Atlas = 2 };
constexpr int kEngines = 3;
const char* engineName (Engine e);    // "GHOSTY PRO 2", "POLTERGEIST 2", "GHOST ATLAS"
const char* engineShort (Engine e);   // "GP2", "P2", "ATLAS"
const char* engineBundle (Engine e);  // the installed plug-in: "Ghosty Pro 2.vst3", ...
const char* engineKey (Engine e);     // "gp2", "p2", "atlas" (ids and saved state)
bool engineFromKey (const juce::String& k, Engine& e);

// what a sound is for, from its folder / style (decides what NEXT GOOD picks for a lane)
enum class Role : int { Bass = 0, Lead, Pad, Keys, Pluck, Bell, Fx, Other };
Role roleOf (const juce::String& category);
const char* roleName (Role r);

struct Sound
{
    Engine engine = Engine::GhostyPro2;
    juce::String id;        // stable key, saved with the song: "<engine>:factory:<category>/<name>" or "<engine>:file:<path>"
    juce::String name;      // shown
    juce::String category;  // the synth's own folder / style ("Bass", "Pads", "Keys", ...)
    juce::String group;     // "Factory", a pack's name, "Yours"
    Role role = Role::Other;
    bool factory = true;
    int packIndex = -1;     // factory: the record in the pack
    juce::File file;        // the owner's own: the file
};

class Catalog
{
public:
    static Catalog& get();                       // one per process; the factory sounds are there at once
    std::vector<Sound> snapshot() const;         // a copy (any thread but audio)
    bool find (const juce::String& id, Sound& out) const;
    int  version() const { return ver.load(); }  // bumps when the list changes (the disk search finished)
    void rescanAsync();                          // look on disk again (in the background)
    bool scanning() const { return busy.load(); }

    // the sound as the synth's own state (what the synth saves into a project), and that wrapped as the state JUCE's
    // VST3 host hands to a VST3 (an XML with the component state in base64): GHOST's lane loads it with setStateInformation
    static bool nativeState (const Sound& s, juce::MemoryBlock& out, juce::String& error);
    static bool hostState (const Sound& s, juce::MemoryBlock& out, juce::String& error);
    static juce::MemoryBlock wrapForVst3Host (const juce::MemoryBlock& componentState);

    // where the owner's own sounds live (the synths' own places)
    static juce::Array<juce::File> vitalFolders();
    static juce::File poltergeistUserFolder();
    static juce::File atlasPresetFolder();

    // test hooks
    static std::vector<Sound> factorySounds();
    static std::vector<Sound> diskSounds();

public:
    ~Catalog();
private:
    Catalog();
    mutable std::mutex lock;
    std::vector<Sound> sounds;
    std::atomic<int> ver { 1 };
    std::atomic<bool> busy { false };
    std::unique_ptr<juce::Thread> worker;
};
} // namespace ghost
