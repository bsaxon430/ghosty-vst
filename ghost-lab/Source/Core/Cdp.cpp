#include "Cdp.h"

namespace glab
{
Cdp::Cdp (juce::File programsDir, juce::File workRoot) : bin (std::move (programsDir)), root (std::move (workRoot))
{
    root.createDirectory();
}

static bool hasPvoc (const juce::File& d) { return d.getChildFile ("pvoc").existsAsFile(); }

juce::File Cdp::findProgramsDir()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("GHOSTLAB_CDP_DIR", {});
    if (env.isNotEmpty() && hasPvoc (juce::File (env))) return juce::File (env);

    // in a plug-in this is the plug-in's own binary (JUCE asks dladdr), in the app the app's binary
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto d = exe.getParentDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
    {
        if (hasPvoc (d.getChildFile ("Helpers/cdp"))) return d.getChildFile ("Helpers/cdp");
        if (hasPvoc (d.getChildFile ("cdp"))) return d.getChildFile ("cdp");
        if (d.getFileName() == "Contents") break;
    }
   #ifdef GHOSTLAB_CDP_DEV_DIR
    if (hasPvoc (juce::File (GHOSTLAB_CDP_DEV_DIR))) return juce::File (GHOSTLAB_CDP_DEV_DIR);
   #endif
    return {};
}

juce::File Cdp::defaultWorkRoot()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Caches/GhostLab/work");
   #else
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GhostLab/work");
   #endif
}

bool Cdp::available() const
{
    return bin != juce::File() && hasPvoc (bin);
}

juce::File Cdp::newJobFolder() const
{
    root.createDirectory();
    auto f = root.getChildFile ("job-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    f.createDirectory();
    return f;
}

void Cdp::cleanStale() const
{
    auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (1);
    for (auto& f : root.findChildFiles (juce::File::findDirectories, false, "job-*"))
        if (f.getLastModificationTime() < cutoff) f.deleteRecursively();
}

CdpResult Cdp::run (juce::StringArray args, const juce::File& job, const juce::File& expectOutput,
                    const std::atomic<bool>* cancel, int timeoutMs) const
{
    CdpResult r;
    if (args.isEmpty()) { r.error = "Nothing to run."; return r; }
    const auto program = args[0];
    auto exe = bin.getChildFile (program);
    if (! exe.existsAsFile()) { r.error = "Ghost Lab's sound engine is missing (" + program + "). Reinstall Ghost Lab."; return r; }
    if (expectOutput != juce::File()) expectOutput.deleteFile();   // CDP refuses to overwrite a file

    // the program runs inside the job folder with its output going to a log file (never a pipe that could fill up and stall it)
    auto logFile = job.getChildFile ("cdp-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()) + ".log");
    juce::StringArray cmd { "/bin/sh", "-c", "cd \"$1\" || exit 97; log=\"$2\"; shift 2; exec \"$@\" >\"$log\" 2>&1",
                            "sh", job.getFullPathName(), logFile.getFullPathName(), exe.getFullPathName() };
    for (int i = 1; i < args.size(); ++i) cmd.add (args[i]);

    juce::ChildProcess p;
    if (! p.start (cmd, 0)) { r.error = "Ghost Lab couldn't start its sound engine (" + program + ")."; return r; }
    const auto t0 = juce::Time::getMillisecondCounter();
    bool killed = false;
    while (! p.waitForProcessToFinish (25))
    {
        if ((cancel != nullptr && cancel->load()) || (int) (juce::Time::getMillisecondCounter() - t0) > timeoutMs)
        {
            p.kill(); killed = true; break;
        }
    }
    r.log = logFile.loadFileAsString();
    logFile.deleteFile();
    if (killed)
    {
        r.error = (cancel != nullptr && cancel->load()) ? "Stopped." : "That took too long and was stopped. Try a shorter sound.";
        return r;
    }
    const auto code = p.getExitCode();
    const bool outOk = expectOutput == juce::File() || (expectOutput.existsAsFile() && expectOutput.getSize() > 0);
    if (code != 0 || ! outOk)
    {
        r.error = friendlyCdpError (program, r.log);
        return r;
    }
    r.ok = true;
    return r;
}

juce::String friendlyCdpError (const juce::String& program, const juce::String& log)
{
    juce::String detail;
    for (auto& line : juce::StringArray::fromLines (log))
        if (line.containsIgnoreCase ("ERROR")) { detail = line.fromFirstOccurrenceOf ("ERROR", false, true).trimCharactersAtStart (": ").trim(); break; }
    auto msg = juce::String ("That didn't work on this sound");
    if (detail.containsIgnoreCase ("too short") || detail.containsIgnoreCase ("insufficient")) msg = "This sound is too short for that";
    else if (detail.containsIgnoreCase ("memory")) msg = "This sound is too long for that";
    if (detail.isNotEmpty()) msg << " (" << program << ": " << detail.substring (0, 80) << ")";
    return msg + ".";
}
}
