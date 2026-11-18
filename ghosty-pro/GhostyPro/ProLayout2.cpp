#include "ProLayout2.h"
#include <set>

namespace gpro::layout2
{
// ---------------------------------------------------------------- the per-user LAYOUT choice
juce::File settingsFile()
{
    if (const char* env = std::getenv ("GHOSTY_SETTINGS_FILE"); env != nullptr && *env != 0) return juce::File (juce::String::fromUTF8 (env));
    juce::PropertiesFile::Options o;
    o.applicationName = "Ghosty Pro"; o.folderName = "GhostBlock"; o.filenameSuffix = "settings"; o.osxLibrarySubFolder = "Application Support";
    return o.getDefaultFile();
}

static std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Ghosty Pro"; o.filenameSuffix = "settings"; o.storageFormat = juce::PropertiesFile::storeAsXML;
    return std::make_unique<juce::PropertiesFile> (settingsFile(), o);
}

int savedLayout()
{
    auto f = openSettings();
    return f->getValue ("layout", "studio").trim().equalsIgnoreCase ("classic") ? classic : studio;     // STUDIO for everyone (owner, 2026-10-25)
}

void saveLayout (int layout)
{
    auto f = openSettings();
    f->setValue ("layout", layout == studio ? "studio" : "classic");
    f->saveIfNeeded();
}

bool ambientOn() { return openSettings()->getBoolValue ("ambient", true); }
void saveAmbient (bool on) { auto f = openSettings(); f->setValue ("ambient", on); f->saveIfNeeded(); }

juce::String layoutName (int layout) { return layout == studio ? "STUDIO" : "CLASSIC"; }

// ---------------------------------------------------------------- tracer wish (message thread)
static std::multiset<const void*>& traceWishes() { static std::multiset<const void*> s; return s; }
void wantTrace (const void* p, bool on)
{
    auto& s = traceWishes();
    if (on) s.insert (p); else if (auto it = s.find (p); it != s.end()) s.erase (it);
}
bool traceWanted (const void* p) { return traceWishes().count (p) > 0; }

} // namespace gpro::layout2
