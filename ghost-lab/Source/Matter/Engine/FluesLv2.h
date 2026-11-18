// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// Flues (danja/flues @ 2eae800, MIT, Danny Ayers) run through its OWN LV2 plug-in code: pm_synth_plugin.cpp,
// chatterbox_plugin.cpp and disyn_plugin.cpp are vendored unmodified (third_party/flues/lv2) and compiled in; this is a tiny
// LV2 host (our glue): it instantiates the plug-in from its own descriptor, sets its control ports (starting from the
// defaults in its own .ttl, also vendored), sends it MIDI note on / off as LV2 atoms and calls its run().
// Worker thread (allocates).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace matter::fluesplug
{
enum class Plugin { pmSynth, chatterbox, disyn };

// the plug-in's control ports, by their .ttl symbol, with the .ttl defaults
std::map<std::string, float> defaults (Plugin p);

struct Note { int note = 48; int velocity = 110; double offAt = 0.75; };   // offAt: fraction of the render

// renders `seconds` of the plug-in playing `note` with `ports` (missing ports keep their .ttl default), at `sr`.
// Mono plug-ins come back in both channels; Disyn is stereo.
juce::AudioBuffer<float> render (Plugin p, const std::map<std::string, float>& ports, const Note& n, double seconds, double sr);

// one live instance of a mono Flues plug-in for the audio thread: made (allocating) on another thread, then note on / off
// and run() never allocate. Ports keep the values given at creation unless set().
class Instance
{
public:
    Instance (Plugin p, const std::map<std::string, float>& ports, double sr);
    ~Instance();
    bool ok() const noexcept { return handle != nullptr; }
    void set (const char* symbol, float value) noexcept;
    void noteOn (int note, int velocity, int frame) noexcept;    // frame: offset into the next run()
    void noteOff (int note, int frame) noexcept;
    void run (float* out, int n) noexcept;                       // n <= 256; writes the plug-in's output into out
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void* handle = nullptr;
};

// the .ttl file's own defaults, parsed from the vendored file (tests compare them with defaults())
std::map<std::string, float> parseTtlDefaults (const juce::String& ttlText);
}
