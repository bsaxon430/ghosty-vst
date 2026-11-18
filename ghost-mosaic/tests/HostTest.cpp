// Ghost Mosaic host test (GPL-2.0-or-later, like the rest of this fork).
//
//   GhostMosaicHostTest <Ghost Mosaic.vst3 | Ghost Mosaic.component> [sampleRate] [varying]
//
// Loads the real, built plug-in as an INSTRUMENT the way FL Studio does (VST3 or AU, stereo out, no audio in),
// opens its screen in a window, and runs tests/HostScenarios.h on it: a new instance, your own sources with no
// target and with a target, GENERATE pressed through the screen's own button, the transport stopped and playing,
// PLAY BY play button / DAW play / MIDI keys, a long song. Every check is that it makes sound (or says why not).
// Exit code = number of failures.
#include <juce_audio_utils/juce_audio_utils.h>
#include "HostScenarios.h"
#include <dlfcn.h>
#include <cstdlib>

static int gFails = 0, gPasses = 0;
static void check (bool ok, const juce::String& name, const juce::String& detail)
{
    std::printf ("%s %s%s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.isNotEmpty() ? (" -- " + detail).toRawUTF8() : "");
    std::fflush (stdout);
    (ok ? gPasses : gFails)++;
}
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

using HookFn = const char* (*) (const char*, const char*);

static HookFn findHook (const juce::File& bundle)
{
    juce::Array<juce::File> bins;
    const auto name = bundle.getFileNameWithoutExtension();
    bins.add (bundle.getChildFile ("Contents/MacOS").getChildFile (name));
    bins.add (bundle.getChildFile ("Contents/x86_64-linux").getChildFile (name + ".so"));
    bins.add (bundle.getChildFile ("Contents/aarch64-linux").getChildFile (name + ".so"));
    for (auto& b : bins)
    {
        if (! b.existsAsFile()) continue;
        for (auto path : { b.getFullPathName(), b.getLinkedTarget().getFullPathName() })
            if (void* h = dlopen (path.toRawUTF8(), RTLD_NOW | RTLD_NOLOAD))
                if (auto* f = (HookFn) dlsym (h, "ghostmosaic_host_test")) return f;
    }
    return (HookFn) dlsym (RTLD_DEFAULT, "ghostmosaic_host_test");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: GhostMosaicHostTest <plugin bundle> [rate] [varying]\n"); return 1; }
    ::setenv ("GHOSTMOSAIC_HOST_TEST", "1", 1);
    ::setenv ("MOSAIC_LONG_SECONDS", std::getenv ("MOSAIC_LONG_SECONDS") ? std::getenv ("MOSAIC_LONG_SECONDS") : "50", 1);
    const juce::File bundle (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (juce::CharPointer_UTF8 (argv[1]))));
    const double sr = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 44100.0;
    const bool varying = argc > 3 && juce::String (argv[3]) == "varying";

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> types;
    juce::String formatName;
    for (auto* f : fm.getFormats())
    {
        if (! f->fileMightContainThisPluginType (bundle.getFullPathName())) continue;
        f->findAllTypesForFile (types, bundle.getFullPathName());
        if (! types.isEmpty()) { formatName = f->getName(); break; }
    }
    check (! types.isEmpty(), "the host finds the plug-in", bundle.getFullPathName() + (formatName.isNotEmpty() ? " (" + formatName + ")" : juce::String()));
    if (types.isEmpty()) return 1;
    auto* desc = types[0];
    for (auto* t : types) if (t->isInstrument) desc = t;
    check (desc->isInstrument, "it is an instrument", desc->name + ", " + desc->pluginFormatName + ", " + desc->category);

    juce::String err;
    auto inst = fm.createPluginInstance (*desc, sr, 512, err);
    check (inst != nullptr, "the host makes an instance", err);
    if (inst == nullptr) return 1;
    pump (200);

    // FL Studio's layout for an instrument: stereo out, no audio in
    auto layout = inst->getBusesLayout();
    for (auto& b : layout.inputBuses) b = juce::AudioChannelSet::disabled();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    inst->setBusesLayout (layout);
    for (int i = inst->getBusCount (true); --i >= 0;) inst->getBus (true, i)->enable (false);

    // its screen, in a window, as the host shows it
    std::unique_ptr<juce::DocumentWindow> win;
   #if JUCE_MAC
    const bool screen = true;
   #else
    const bool screen = false;   // (two JUCE copies in one process can't share an X11 display here)
   #endif
    if (auto* ed = screen ? inst->createEditorIfNeeded() : nullptr)
    {
        win = std::make_unique<juce::DocumentWindow> ("Ghost Mosaic (host test)", juce::Colours::black, 0, true);
       #if JUCE_MAC
        win->setUsingNativeTitleBar (true);
       #endif
        win->setContentNonOwned (ed, true);
        win->setTopLeftPosition (40, 40);
        win->setVisible (true);
    }
    pump (1500);

    auto hook = findHook (bundle);
    check (hook != nullptr, "the plug-in's test hook is there", {});
    if (hook == nullptr) return 1;
    hostscen::Env env { *inst, [hook] (const char* c, const juce::String& a) { return juce::String (juce::CharPointer_UTF8 (hook (c, a.toRawUTF8()))); },
                        [] (bool ok, const juce::String& n, const juce::String& d) { check (ok, n, d); }, [] (int ms) { pump (ms); }, desc->pluginFormatName, screen };
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostmosaic-hosttest");
    dir.createDirectory();
    hostscen::run (env, sr, varying, dir);

    if (win != nullptr) { win->clearContentComponent(); }
    if (auto* ed = inst->getActiveEditor()) delete ed;
    win.reset();
    pump (200);
    inst.reset();
    pump (200);
    std::printf ("\n%d passed, %d failed\n", gPasses, gFails);
    return gFails;
}
