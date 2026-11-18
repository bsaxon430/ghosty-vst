// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md). Starts the CDP8 programs (Cdp.h).
#include "Cdp.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace matter::cdp
{
namespace
{
juce::CriticalSection recentLock;
juce::StringArray recent;
}
juce::StringArray recentCommands() { const juce::ScopedLock l (recentLock); return recent; }
void clearRecentCommands() { const juce::ScopedLock l (recentLock); recent.clear(); }

static bool hasPvoc (const juce::File& d) { return d.getChildFile ("pvoc").existsAsFile(); }

juce::File programsDir()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("GHOSTMATTER_CDP_DIR", {});
    if (env.isNotEmpty() && hasPvoc (juce::File (env))) return juce::File (env);
    // in a plug-in this is the plug-in's own binary (JUCE asks dladdr), in the app the app's binary
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto d = exe.getParentDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
    {
        if (hasPvoc (d.getChildFile ("Helpers/cdp"))) return d.getChildFile ("Helpers/cdp");
        if (hasPvoc (d.getChildFile ("cdp"))) return d.getChildFile ("cdp");
        if (d.getFileName() == "Contents") break;
    }
   #ifdef GHOSTMATTER_CDP_DEV_DIR
    if (hasPvoc (juce::File (GHOSTMATTER_CDP_DEV_DIR))) return juce::File (GHOSTMATTER_CDP_DEV_DIR);
   #endif
    return {};
}

bool available() { return programsDir() != juce::File(); }

juce::File newJob()
{
   #if JUCE_MAC
    auto root = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Caches/GhostMatter/work");
   #else
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GhostMatter/work");
   #endif
    root.createDirectory();
    auto f = root.getChildFile ("job-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    f.createDirectory();
    return f;
}

Result run (const juce::StringArray& args, const juce::File& job, const juce::File& expectOutput, const std::atomic<bool>* cancel, int timeoutMs)
{
    Result r;
    if (args.isEmpty()) { r.error = "nothing to run"; return r; }
    {
        const juce::ScopedLock l (recentLock);
        recent.add (args.joinIntoString (" "));
        while (recent.size() > 24) recent.remove (0);
    }
    auto exe = programsDir().getChildFile (args[0]);
    if (! exe.existsAsFile()) { r.error = "a morph program is missing (" + args[0] + ")"; return r; }
    if (expectOutput != juce::File()) expectOutput.deleteFile();   // CDP refuses to overwrite a file
    // runs inside the job folder, its output to a log file (never a pipe that could fill up and stall it)
    auto logFile = job.getChildFile ("cdp-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()) + ".log");
    juce::StringArray cmd { "/bin/sh", "-c", "cd \"$1\" || exit 97; log=\"$2\"; shift 2; exec \"$@\" >\"$log\" 2>&1",
                            "sh", job.getFullPathName(), logFile.getFullPathName(), exe.getFullPathName() };
    for (int i = 1; i < args.size(); ++i) cmd.add (args[i]);
    juce::ChildProcess p;
    if (! p.start (cmd, 0)) { r.error = "a morph program would not start (" + args[0] + ")"; return r; }
    const auto t0 = juce::Time::getMillisecondCounter();
    bool killed = false;
    while (! p.waitForProcessToFinish (25))
        if ((cancel != nullptr && cancel->load()) || (int) (juce::Time::getMillisecondCounter() - t0) > timeoutMs) { p.kill(); killed = true; break; }
    r.log = logFile.loadFileAsString();
    logFile.deleteFile();
    if (killed) { r.error = "stopped"; return r; }
    const bool outOk = expectOutput == juce::File() || (expectOutput.existsAsFile() && expectOutput.getSize() > 0);
    if (p.getExitCode() != 0 || ! outOk)
    {
        r.error = args[0] + " " + args[1] + ": ";
        for (auto& line : juce::StringArray::fromLines (r.log))
            if (line.containsIgnoreCase ("ERROR")) { r.error << line.fromFirstOccurrenceOf ("ERROR", false, true).trimCharactersAtStart (": ").trim().substring (0, 80); break; }
        return r;
    }
    r.ok = true;
    return r;
}

bool writeMonoWav (const juce::File& f, const float* x, int n, double rate)
{
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, 1, 32, {}, 0));
    if (w == nullptr) return false;
    os.release();
    const float* chans[] = { x };
    return w->writeFromFloatArrays (chans, 1, n);
}

std::vector<float> readMonoWav (const juce::File& f)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (f.createInputStream().release(), true));
    if (r == nullptr || r->lengthInSamples <= 0) return {};
    juce::AudioBuffer<float> b (1, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, false);
    return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
}
}
