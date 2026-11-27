#pragma once
// GHOST - opening the owner's installed plug-ins inside GHOST.
#include <juce_audio_processors/juce_audio_processors.h>

namespace ghost
{
struct Hosted
{
    // where VST3 plug-ins are installed on this computer (user folder first, then the system folder)
    static juce::Array<juce::File> vst3Folders();
    // the installed bundle with this file name ("ArpEZator.vst3"), or {} when it isn't installed
    static juce::File findBundle (const juce::String& bundleName);
    // opens it (message thread). null + error when it can't
    static std::unique_ptr<juce::AudioPluginInstance> open (const juce::String& bundleName, double rate, int block, juce::String& error);
    static juce::AudioPluginFormatManager& formats();
};
} // namespace ghost
