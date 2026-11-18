// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// Runs the CDP8 programs (ComposersDesktop/CDP8 @ 28bc42c, LGPL-2.1+), shipped unmodified as separate executables inside
// the bundle (Contents/Helpers/cdp; built by cdp/build-cdp.sh). Our code never links CDP: it only starts the programs,
// like a user typing them in a terminal. (The same runner as Ghost Lab's, GhostBlock's own code.) Never on the audio thread.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>

namespace matter::cdp
{
// $GHOSTMATTER_CDP_DIR, then the bundle's Contents/Helpers/cdp, then a "cdp" folder next to the executable, then the
// development folder compiled in (GHOSTMATTER_CDP_DEV_DIR). Empty when CDP is not there.
juce::File programsDir();
bool available();

struct Result { bool ok = false; juce::String error, log; };

// one CDP program: args[0] is its name ("pvoc"), the rest its arguments, run inside `job`
Result run (const juce::StringArray& args, const juce::File& job, const juce::File& expectOutput,
            const std::atomic<bool>* cancel = nullptr, int timeoutMs = 180000);

// the command lines the last runs used, newest last (for UNDER THE HOOD: "show what it ran, for trust"); thread-safe copy
juce::StringArray recentCommands();
void clearRecentCommands();

// a fresh folder for one job (the caller deletes it)
juce::File newJob();

// mono 32-bit float WAV in / out (what CDP's sfsys reads and writes)
bool writeMonoWav (const juce::File& f, const float* x, int n, double rate);
std::vector<float> readMonoWav (const juce::File& f);
}
