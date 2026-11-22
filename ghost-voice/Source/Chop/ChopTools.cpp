// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "ChopTools.h"
#include "VoiceEngine.h"

namespace gv::chop
{
Tools::Tools() : juce::Thread ("Ghost Voice chop tools") {}
Tools::~Tools() { alive->store (false); cancel(); }

juce::File Tools::dir()
{
    const auto e = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_CHOPTOOLS", {});
    return e.isNotEmpty() ? juce::File (e) : workDir().getChildFile ("choptools");
}

juce::File Tools::script() { return helperDir().getChildFile ("chop_tools.py"); }

juce::String Tools::pythonFor (Kind k) const
{
    const auto forced = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_CHOP_PYTHON", {});   // tests / developers
    if (forced.isNotEmpty()) return forced;
    const auto py = dir().getChildFile ("venv/bin/python");
    if (k == Kind::words) return py.existsAsFile() && dir().getChildFile ("words.ok").existsAsFile() ? py.getFullPathName() : juce::String();
    if (py.existsAsFile() && dir().getChildFile ("phrases.ok").existsAsFile()) return py.getFullPathName();
    if (engineInstalled() && enginePython().existsAsFile()) return enginePython().getFullPathName();   // the voice engine's Python has numpy
    return {};
}

juce::String Tools::progress() const { const juce::ScopedLock l (lock); return step; }

bool Tools::install (Kind k, std::function<void (bool)> onDone)
{
    if (busy()) return false;
    const auto sh = helperDir().getChildFile ("install-chop-tools.sh");
    if (! sh.existsAsFile()) { lastError = "the installer is missing from this build"; return false; }
    kind = k; installing = true; installDone = std::move (onDone); runDone = nullptr;
    cmd = { "/usr/bin/env", "GHOSTVOICE_CHOPTOOLS=" + dir().getFullPathName(), "/bin/bash", sh.getFullPathName(), k == Kind::words ? "words" : "phrases" };
    { const juce::ScopedLock l (lock); step = k == Kind::words ? "Installing the word finder..." : "Installing the phrase finder..."; }
    lastError = {};
    startThread();
    return true;
}

juce::StringArray Tools::commandFor (Kind k, const juce::String& py, const juce::File& wav, const juce::File& out, const juce::StringArray& extra) const
{
    juce::StringArray c { "/usr/bin/env", "HF_HOME=" + dir().getChildFile ("hf").getFullPathName(), "TORCH_HOME=" + dir().getChildFile ("torch").getFullPathName(),
                          "PYTHONNOUSERSITE=1", py, script().getFullPathName(), k == Kind::words ? "words" : "phrases", wav.getFullPathName(), out.getFullPathName() };
    if (k == Kind::phrases) c.addArray (extra);
    return c;
}

bool Tools::run (Kind k, const juce::File& wav, std::function<void (bool, std::vector<ToolChop>)> onDone, const juce::StringArray& extra)
{
    if (busy()) return false;
    const auto py = pythonFor (k);
    if (py.isEmpty()) { lastError = "not installed"; return false; }
    kind = k; installing = false; runDone = std::move (onDone); installDone = nullptr;
    outFile = juce::File::createTempFile (".json");
    cmd = commandFor (k, py, wav, outFile, extra);
    { const juce::ScopedLock l (lock); step = k == Kind::words ? "Finding the words..." : "Finding the phrases..."; }
    lastError = {};
    startThread();
    return true;
}

void Tools::cancel() { signalThreadShouldExit(); stopThread (4000); }

void Tools::run()
{
    juce::ChildProcess p;
    bool ok = false; juce::String tail;
    std::vector<ToolChop> chops;
    if (p.start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        juce::String buf;
        while (! threadShouldExit())
        {
            char b[512]; const int n = p.readProcessOutput (b, sizeof (b));
            if (n > 0)
            {
                buf += juce::String::fromUTF8 (b, n);
                while (buf.containsChar ('\n'))
                {
                    const auto line = buf.upToFirstOccurrenceOf ("\n", false, false).trim(); buf = buf.fromFirstOccurrenceOf ("\n", false, false);
                    if (line.startsWith ("GV_STEP")) { const juce::ScopedLock l (lock); step = line.fromFirstOccurrenceOf (" ", false, false).fromFirstOccurrenceOf (" ", false, false); }
                    if (line.isNotEmpty()) tail = line;
                }
                continue;
            }
            if (! p.isRunning()) break;
            wait (30);
        }
        if (threadShouldExit()) { p.kill(); tail = "cancelled"; }
        else ok = p.getExitCode() == 0 && (installing ? tail.startsWith ("GV_INSTALLED") || buf.contains ("GV_INSTALLED") : true);
    }
    else tail = "could not start Python";
    if (! installing && ok)
    {
        const auto v = juce::JSON::parse (outFile);
        ok = (bool) v["ok"];
        if (ok) for (auto& c : *v["chops"].getArray()) chops.push_back ({ (double) c["a"], (double) c["b"], c["text"].toString() });
        else tail = v["error"].toString();
    }
    outFile.deleteFile();
    if (! ok) lastError = uiWords (tail.startsWith ("GV_FAILED") ? tail.fromFirstOccurrenceOf (" ", false, false) : tail);
    { const juce::ScopedLock l (lock); step = {}; }
    auto token = alive; auto inst = installing;
    auto id = installDone; auto rd = runDone;
    juce::MessageManager::callAsync ([token, ok, chops, inst, id, rd]
    {
        if (! token->load()) return;
        if (inst) { if (id) id (ok); }
        else if (rd) rd (ok, chops);
    });
}
}
