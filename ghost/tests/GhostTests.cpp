// GHOST checks without any plug-in: the factory sounds are all there, every one turns into a state its synth reads.
#include "Catalog.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>

using namespace ghost;
static int fails = 0;
#define CHECK(c, msg) do { if (! (c)) { ++fails; std::printf ("FAIL: %s\n", juce::String (msg).toRawUTF8()); } } while (0)

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI init;
    auto f = Catalog::factorySounds();
    int per[kEngines] {}; int roles[8] {};
    for (auto& s : f) { per[(int) s.engine]++; roles[(int) s.role]++; }
    std::printf ("factory: %d GP2, %d P2, %d ATLAS\n", per[0], per[1], per[2]);
    CHECK (per[0] == 34, "34 Ghosty Pro 2 sounds");
    CHECK (per[1] > 190, "Poltergeist 2 sounds");
    CHECK (per[2] > 200, "Ghost Atlas sounds");
    for (int r = 0; r < 8; ++r) std::printf ("  %-6s %d\n", roleName ((Role) r), roles[r]);
    for (Role r : { Role::Bass, Role::Pad, Role::Lead, Role::Pluck }) CHECK (roles[(int) r] >= 10, juce::String ("enough ") + roleName (r));
    juce::StringArray ids;
    for (auto& s : f)
    {
        CHECK (! ids.contains (s.id), "unique id " + s.id);
        ids.add (s.id);
        juce::MemoryBlock raw, host; juce::String err;
        CHECK (Catalog::nativeState (s, raw, err), "state of " + s.id + ": " + err);
        if (s.engine == Engine::GhostyPro2)
        {
            auto j = juce::JSON::parse (raw.toString());
            CHECK (j.isObject() && j.hasProperty ("settings"), "vital json " + s.id);
        }
        if (s.engine == Engine::Poltergeist2)
        {
            CHECK (raw.getSize() > 10 && ((const char*) raw.getData())[raw.getSize() - 1] == 0, "p2 ends with its zero " + s.id);
            CHECK (raw.toString().contains ("<patch"), "p2 patch xml " + s.id);
        }
        if (s.engine == Engine::Atlas)
        {
            auto j = juce::JSON::parse (raw.toString());
            CHECK (j.getProperty ("format", "").toString() == "ghost-atlas-state" && j.getProperty ("preset", {}).isObject(), "atlas state " + s.id);
        }
        CHECK (Catalog::hostState (s, host, err), "host state " + s.id);
        auto xml = juce::AudioProcessor::getXmlFromBinary (host.getData(), (int) host.getSize());
        CHECK (xml != nullptr && xml->hasTagName ("VST3PluginState"), "vst3 host xml " + s.id);
        if (xml)
        {
            juce::MemoryBlock back; back.fromBase64Encoding (xml->getChildByName ("IComponent")->getAllSubText());
            CHECK (back == raw, "round trip " + s.id);
        }
    }
    Sound s; CHECK (Catalog::get().find ("gp2:factory:Pad/Warm Blanket", s), "default CHORDS sound");
    CHECK (Catalog::get().find ("gp2:factory:Pluck/Crystal Pluck", s), "default ARP sound");
    CHECK (Catalog::get().find ("gp2:factory:Lead/Ghost Whistle", s), "default MELODY sound");
    CHECK (Catalog::get().find ("gp2:factory:Bass/Velvet 808", s), "default BASS sound");
    Catalog::get().rescanAsync(); while (Catalog::get().scanning()) juce::Thread::sleep (20);
    std::printf (fails == 0 ? "ALL GOOD (%d sounds)\n" : "%d FAILED\n", fails == 0 ? (int) f.size() : fails);
    return fails == 0 ? 0 : 1;
}
