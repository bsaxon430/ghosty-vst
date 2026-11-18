#pragma once
// After a host state load, a CLAP host must be told that parameter values changed (clap_host_params.rescan(VALUES)), or it keeps
// showing / automating from the old values (clap-validator: "parameter values changed without a rescan request").
// clap-juce-extensions turns updateHostDisplay (programChanged) into exactly that rescan. CLAP builds are the only plugin builds
// whose wrapperType is Undefined (VST3 / AU / Standalone set theirs), so those formats are left exactly as before; console test
// harnesses are Undefined too but have no listeners, so nothing happens there.
// Use: first line of setStateInformation:  ghosty::StateLoadNotify notifyHost (*this);   (fires on every return path)
#include <juce_audio_processors/juce_audio_processors.h>

namespace ghosty
{
struct StateLoadNotify
{
    explicit StateLoadNotify (juce::AudioProcessor& proc) noexcept : p (proc) {}
    ~StateLoadNotify()
    {
        if (p.wrapperType == juce::AudioProcessor::wrapperType_Undefined)
            p.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
    }
    juce::AudioProcessor& p;
    JUCE_DECLARE_NON_COPYABLE (StateLoadNotify)
};
} // namespace ghosty
