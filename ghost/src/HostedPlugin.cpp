#include "HostedPlugin.h"

namespace ghost
{
juce::AudioPluginFormatManager& Hosted::formats()
{
    static juce::AudioPluginFormatManager m;
    if (m.getNumFormats() == 0) m.addFormat (new juce::VST3PluginFormat());
    return m;
}

juce::Array<juce::File> Hosted::vst3Folders()
{
    juce::Array<juce::File> v;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto env = juce::SystemStats::getEnvironmentVariable ("GHOST_VST3_DIR", {});   // (checks: a folder of test plug-ins)
    if (env.isNotEmpty()) v.add (juce::File (env));
#if JUCE_MAC
    v.add (home.getChildFile ("Library/Audio/Plug-Ins/VST3"));
    v.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
#elif JUCE_LINUX
    v.add (home.getChildFile (".vst3"));
    v.add (juce::File ("/usr/local/lib/vst3"));
    v.add (juce::File ("/usr/lib/vst3"));
#else
    v.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/VST3"));
#endif
    return v;
}

juce::File Hosted::findBundle (const juce::String& bundleName)
{
    for (auto& dir : vst3Folders())
    {
        const auto f = dir.getChildFile (bundleName);
        if (f.exists()) return f;
    }
    return {};
}

std::unique_ptr<juce::AudioPluginInstance> Hosted::open (const juce::String& bundleName, double rate, int block, juce::String& error)
{
    const auto bundle = findBundle (bundleName);
    if (bundle == juce::File())
    {
        error = bundleName.upToLastOccurrenceOf (".vst3", false, true) + " isn't installed";
        return nullptr;
    }
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile (types, bundle.getFullPathName());
    if (types.isEmpty())
    {
        error = "couldn't open " + bundle.getFileName();
        return nullptr;
    }
    // a bundle can hold more than one plug-in (Ghost Lab has an FX one): the instrument first
    const juce::PluginDescription* pick = types[0];
    for (auto* t : types) if (t->isInstrument) { pick = t; break; }
    juce::String err;
    auto inst = formats().createPluginInstance (*pick, rate, block, err);
    if (inst == nullptr) { error = err.isNotEmpty() ? err : "couldn't start " + bundle.getFileName(); return nullptr; }
    // GHOST reads the main output's first two channels. A plug-in's own layout is kept as it is; only if it comes up with no
    // output (some VST3s start with their buses off) is the main output switched on as stereo.
    if (inst->getTotalNumOutputChannels() < 1)
    {
        auto layout = inst->getBusesLayout();
        if (! layout.outputBuses.isEmpty()) { layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo(); inst->setBusesLayout (layout); }
    }
    if (inst->getTotalNumOutputChannels() < 1) inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (rate, block);
    inst->prepareToPlay (rate, block);
    return inst;
}
} // namespace ghost
