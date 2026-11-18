#pragma once
// Runs the CDP8 programs (LGPL-2.1+, shipped unmodified as separate executables inside the bundle: Contents/Helpers/cdp).
// Our code never links CDP: it only starts the programs, like a user typing them in a terminal.
#include <juce_core/juce_core.h>
#include <atomic>

namespace glab
{
struct CdpResult
{
    bool ok = false;
    juce::String error;   // plain words for the screen
    juce::String log;     // what the program printed (for the deep view / tests)
};

class Cdp
{
public:
    Cdp (juce::File programsDir, juce::File workRoot);

    // where the CDP programs are: $GHOSTLAB_CDP_DIR, then the bundle's Contents/Helpers/cdp, then a "cdp" folder next to
    // the executable, then the folder compiled in for development builds (GHOSTLAB_CDP_DEV_DIR)
    static juce::File findProgramsDir();
    // the sandboxed cache folder all temp files live in (~/Library/Caches/GhostLab on the Mac)
    static juce::File defaultWorkRoot();

    bool available() const;              // pvoc is there and can be started
    juce::File programsDir() const { return bin; }

    // a fresh, empty folder for one render (deleted by the caller with juce::File::deleteRecursively)
    juce::File newJobFolder() const;

    // run one CDP program: args[0] is the program name ("pvoc"), the rest its arguments. Never on the audio thread.
    // expectOutput: the file the program must have written (empty = don't check)
    CdpResult run (juce::StringArray args, const juce::File& jobFolder, const juce::File& expectOutput,
                   const std::atomic<bool>* cancel = nullptr, int timeoutMs = 180000) const;

    // removes job folders a crash left behind (older than a day)
    void cleanStale() const;

private:
    juce::File bin, root;
};

juce::String friendlyCdpError (const juce::String& program, const juce::String& log);
}
