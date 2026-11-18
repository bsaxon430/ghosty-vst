// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Words.h"
#include "MakeStage.h"

namespace matter
{
juce::File wordsHelper()
{
    auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir = self.getParentDirectory(); dir.exists() && dir != dir.getParentDirectory(); dir = dir.getParentDirectory())
    {
        auto h = dir.getChildFile ("Helpers").getChildFile ("ghostmatter-words");
        if (h.existsAsFile()) return h;
        if (dir.getFileName() == "Contents") break;
    }
    auto app = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GhostBlock").getChildFile ("Ghost Matter").getChildFile ("ghostmatter-words");
    if (app.existsAsFile()) return app;
    if (auto env = juce::SystemStats::getEnvironmentVariable ("GHOSTMATTER_WORDS", {}); env.isNotEmpty() && juce::File (env).existsAsFile())
        return juce::File (env);
    return {};
}

juce::String barkSpeaker (int body) { return "v2/en_speaker_" + juce::String (juce::jlimit (0, 9, body)); }

bool runWords (const juce::String& text, int speaker, juce::MemoryBlock& flacOut, juce::String& why, const hood::Pins* pins)
{
    auto helper = wordsHelper();
    if (! helper.existsAsFile()) { why = "the words add-on is not installed"; return false; }
    const auto clean = text.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?'-").substring (0, 200).trim();
    if (clean.isEmpty()) { why = "type some words first"; return false; }
    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostmatter-words-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()) + ".wav");
    juce::ChildProcess p;
    juce::StringArray args { helper.getFullPathName(), "--text", clean, "--speaker", barkSpeaker (speaker), "--out", out.getFullPathName() };
    if (pins != nullptr)
    {
        if (auto it = pins->find ("bark.speaker"); it != pins->end()) args.set (4, barkSpeaker ((int) std::lround (it->second)));
        if (auto it = pins->find ("bark.text_temp"); it != pins->end()) { args.add ("--text-temp"); args.add (juce::String (it->second, 3)); }
        if (auto it = pins->find ("bark.waveform_temp"); it != pins->end()) { args.add ("--waveform-temp"); args.add (juce::String (it->second, 3)); }
        if (auto it = pins->find ("bark.small"); it != pins->end() && it->second > 0.5f) args.add ("--small");
    }
    if (! p.start (args)) { why = "the words helper would not start"; return false; }
    const bool finished = p.waitForProcessToFinish (600000);
    const auto log = p.readAllProcessOutput();
    bool ok = false;
    if (! finished) { p.kill(); why = "making the words took too long"; }
    else if (p.getExitCode() != 0) why = log.trim().isNotEmpty() ? log.trim().getLastCharacters (120) : "the words helper failed";
    else
    {
        juce::MemoryBlock mb;
        if (out.loadFileAsData (mb))
        {
            auto b = decodeAudio (mb, kMaxSourceSeconds);
            if (b.getNumSamples() > 0) { flacOut = encodeFlac (b); ok = flacOut.getSize() > 0; }
        }
        if (! ok) why = "the words helper gave no sound";
    }
    out.deleteFile();
    return ok;
}
}
