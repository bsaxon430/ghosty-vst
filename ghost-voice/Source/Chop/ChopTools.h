// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// CHOP's optional tools (build 9), each a separate Python process started by the plug-in (helper/chop_tools.py):
//   PHRASES  OpenVPI's audio-slicer (MIT, helper/third_party/openvpi-audio-slicer, untouched): a vocal split at its silences.
//            Needs Python + numpy: the voice engine's Python when it is installed, else CHOP's own (INSTALL, ~60 MB).
//   WORDS    WhisperX 3.3.1 (m-bain/whisperX, BSD-2): every word and where it starts / ends. Needs its own install (~1.5 GB).
// Both go into one folder that can be deleted: <workDir>/choptools (helper/install-chop-tools.sh).
#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <vector>

namespace gv::chop
{
struct ToolChop { double a = 0, b = 0; juce::String text; };

class Tools : private juce::Thread
{
public:
    Tools();
    ~Tools() override;
    enum class Kind { phrases, words };
    static juce::File dir();                       // <workDir>/choptools (GHOSTVOICE_CHOPTOOLS overrides)
    static juce::File script();                    // the bundled helper/chop_tools.py
    juce::String pythonFor (Kind) const;           // empty = not installed
    bool ready (Kind k) const { return pythonFor (k).isNotEmpty(); }
    bool busy() const { return isThreadRunning(); }
    juce::String progress() const;                 // what is happening now
    juce::String lastError;

    // message thread; onDone runs on the message thread
    bool install (Kind, std::function<void (bool ok)> onDone);
    bool run (Kind, const juce::File& wav, std::function<void (bool ok, std::vector<ToolChop>)> onDone, const juce::StringArray& extra = {});
    // the exact command run() starts (extra = OpenVPI's own options for PHRASES; empty = the command it has always been)
    juce::StringArray commandFor (Kind, const juce::String& python, const juce::File& wav, const juce::File& out, const juce::StringArray& extra = {}) const;
    void cancel();

private:
    void run() override;
    juce::StringArray cmd;
    juce::File outFile;
    bool installing = false;
    Kind kind = Kind::phrases;
    std::function<void (bool)> installDone;
    std::function<void (bool, std::vector<ToolChop>)> runDone;
    mutable juce::CriticalSection lock;
    juce::String step;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
}
