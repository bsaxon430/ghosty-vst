#include "Catalog.h"
#include "GhostPresets.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>

namespace ghost
{
const char* engineName (Engine e)   { static const char* n[] { "GHOSTY PRO 2", "POLTERGEIST 2", "GHOST ATLAS" }; return n[juce::jlimit (0, 2, (int) e)]; }
const char* engineShort (Engine e)  { static const char* n[] { "GP2", "P2", "ATLAS" }; return n[juce::jlimit (0, 2, (int) e)]; }
const char* engineBundle (Engine e) { static const char* n[] { "Ghosty Pro 2.vst3", "Poltergeist 2.vst3", "Ghost Atlas.vst3" }; return n[juce::jlimit (0, 2, (int) e)]; }
const char* engineKey (Engine e)    { static const char* n[] { "gp2", "p2", "atlas" }; return n[juce::jlimit (0, 2, (int) e)]; }
bool engineFromKey (const juce::String& k, Engine& e)
{
    for (int i = 0; i < kEngines; ++i) if (k == engineKey ((Engine) i)) { e = (Engine) i; return true; }
    return false;
}

Role roleOf (const juce::String& category)
{
    const auto c = category.toLowerCase();
    auto has = [&] (std::initializer_list<const char*> w) { for (auto* x : w) if (c.contains (x)) return true; return false; };
    if (has ({ "bass", "808", "sub", "reese" })) return Role::Bass;
    if (has ({ "bell", "mallet", "chime" })) return Role::Bell;
    if (has ({ "pluck", "guitar", "sequence", "arp", "pizz", "kalimba" })) return Role::Pluck;
    if (has ({ "lead", "vocal chop", "whistle", "flute" })) return Role::Lead;
    if (has ({ "pad", "choir", "string", "harmony", "ambient", "soundscape", "cinematic", "texture", "drone" })) return Role::Pad;
    if (has ({ "key", "piano", "organ", "rhodes", "brass" })) return Role::Keys;
    if (has ({ "sfx", "effect", "experimental", "granular", "percussion", "drum", "impact", "riser" })) return Role::Fx;
    if (has ({ "synth" })) return Role::Keys;
    return Role::Other;
}
const char* roleName (Role r) { static const char* n[] { "BASS", "LEAD", "PAD", "KEYS", "PLUCK", "BELL", "FX", "OTHER" }; return n[juce::jlimit (0, 7, (int) r)]; }

// ---------------------------------------------------------------- the packs (GHOSTPACK1: "cat\tname\tbytes\n" + bytes + "\n")
namespace
{
struct PackRec { juce::String cat, name; size_t at = 0, len = 0; };
struct Pack { const char* data = nullptr; size_t size = 0; std::vector<PackRec> recs; };

Pack readPack (const char* resource)
{
    Pack p; int n = 0;
    p.data = GhostPresets::getNamedResource (resource, n); p.size = (size_t) juce::jmax (0, n);
    if (p.data == nullptr || p.size < 11 || std::memcmp (p.data, "GHOSTPACK1\n", 11) != 0) return p;
    size_t i = 11;
    while (i < p.size)
    {
        size_t e = i; while (e < p.size && p.data[e] != '\n') ++e;
        if (e >= p.size) break;
        auto head = juce::StringArray::fromTokens (juce::String::fromUTF8 (p.data + i, (int) (e - i)), "\t", "");
        if (head.size() < 3) break;
        PackRec r; r.cat = head[0]; r.name = head[1]; r.len = (size_t) head[2].getLargeIntValue(); r.at = e + 1;
        if (r.at + r.len > p.size) break;
        p.recs.push_back (r);
        i = r.at + r.len + 1;
    }
    return p;
}
const Pack& gp2Pack() { static const Pack p = readPack ("ghostypro2_pack"); return p; }
const Pack& p2Pack()  { static const Pack p = readPack ("poltergeist2_pack"); return p; }
const juce::var& atlasFactory()
{
    static const juce::var v = [] { int n = 0; auto* d = GhostPresets::getNamedResource ("factory_json", n); return d ? juce::JSON::parse (juce::String::fromUTF8 (d, n)) : juce::var(); }();
    return v;
}

juce::String quickVitalStyle (const juce::File& f)
{   // "preset_style" sits near the top of a .vital (keys are written A-Z, after "author" / "comments" / "macro...")
    juce::FileInputStream in (f);
    if (! in.openedOk()) return {};
    juce::MemoryBlock mb; in.readIntoMemoryBlock (mb, 1 << 16);
    const auto text = mb.toString();
    const int k = text.indexOf ("\"preset_style\"");
    if (k < 0) return {};
    const int q1 = text.indexOfChar (text.indexOfChar (k + 14, ':'), '"');
    const int q2 = q1 < 0 ? -1 : text.indexOfChar (q1 + 1, '"');
    return q2 > q1 ? text.substring (q1 + 1, q2) : juce::String();
}

void scanFiles (const juce::File& root, const juce::String& pattern, int maxDepth, juce::Array<juce::File>& out)
{
    if (! root.isDirectory()) return;
    for (const auto& e : juce::RangedDirectoryIterator (root, true, pattern, juce::File::findFiles | juce::File::ignoreHiddenFiles))
    {
        const auto f = e.getFile();
        if (f.getFullPathName().contains ("__MACOSX")) continue;
        int depth = 0; for (auto p = f.getParentDirectory(); p != root && depth < 64; p = p.getParentDirectory()) ++depth;
        if (depth <= maxDepth) out.add (f);
        if (out.size() > 20000) return;
    }
}
} // namespace

std::vector<Sound> Catalog::factorySounds()
{
    std::vector<Sound> v;
    const auto& g = gp2Pack();
    for (int i = 0; i < (int) g.recs.size(); ++i)
    {
        Sound s; s.engine = Engine::GhostyPro2; s.name = g.recs[(size_t) i].name; s.category = g.recs[(size_t) i].cat; s.group = "Factory";
        s.packIndex = i; s.id = "gp2:factory:" + s.category + "/" + s.name; s.role = roleOf (s.category + " " + s.name); v.push_back (s);
    }
    const auto& p = p2Pack();
    for (int i = 0; i < (int) p.recs.size(); ++i)
    {
        Sound s; s.engine = Engine::Poltergeist2; s.name = p.recs[(size_t) i].name; s.category = p.recs[(size_t) i].cat; s.group = "Factory";
        s.packIndex = i; s.id = "p2:factory:" + s.category + "/" + s.name; s.role = roleOf (s.category); v.push_back (s);
    }
    if (auto* a = atlasFactory().getArray())
        for (int i = 0; i < a->size(); ++i)
        {
            const auto& e = a->getReference (i);
            Sound s; s.engine = Engine::Atlas; s.name = e.getProperty ("name", "").toString(); s.category = e.getProperty ("category", "").toString();
            s.group = "Factory"; s.packIndex = i; s.id = "atlas:factory:" + s.name; s.role = roleOf (s.category); v.push_back (s);
        }
    return v;
}

juce::Array<juce::File> Catalog::vitalFolders()
{
    juce::Array<juce::File> list;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    auto add = [&] (const juce::File& f) { if (f != juce::File() && f.isDirectory()) list.addIfNotAlreadyThere (f); };
    auto configured = [] (const juce::File& cfg) -> juce::File
    {
        if (! cfg.existsAsFile()) return {};
        auto v = juce::JSON::parse (cfg.loadFileAsString());
        const auto d = v.getProperty ("data_directory", "").toString();
        return d.isNotEmpty() && juce::File::isAbsolutePath (d) ? juce::File (d) : juce::File();
    };
#if JUCE_MAC
    const auto support = home.getChildFile ("Library/Application Support");
    add (configured (support.getChildFile ("vital/Vital.config")));
    add (home.getChildFile ("Music/Vital"));
    add (configured (support.getChildFile ("vial/Vial.config")));
    add (home.getChildFile ("Music/Vial"));
    add (home.getChildFile ("Library/Audio/Presets/Vital"));
#else
    add (configured (home.getChildFile (".vital/Vital.config")));
    add (home.getChildFile (".local/share/vital"));
#endif
    // the folders the owner added in Ghosty Pro 2 (its settings file: "vitalFolders", one path per line)
    juce::PropertiesFile::Options o; o.applicationName = "Ghosty Pro 2"; o.filenameSuffix = "settings"; o.folderName = "GhostBlock";
    o.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile gp2 (o);
    juce::StringArray paths; paths.addLines (gp2.getValue ("vitalFolders"));
    for (auto& p : paths) if (p.isNotEmpty() && juce::File::isAbsolutePath (p)) add (juce::File (p));
    return list;
}
juce::File Catalog::poltergeistUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GhostBlock/Poltergeist 2/Patches");
}
juce::File Catalog::atlasPresetFolder()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("GHOST_ATLAS_HOME", {});
    const auto root = env.isNotEmpty() ? juce::File (env) : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("GhostBlock/Ghost Atlas");
    return root.getChildFile ("Presets");
}

std::vector<Sound> Catalog::diskSounds()
{
    std::vector<Sound> v;
    juce::StringArray seen;
    // Ghosty Pro 2's own list of what it found last time (the fastest way to every pack the owner has)
    {
        const auto cache = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Ghosty Pro 2/found-presets.txt");
        juce::StringArray lines; lines.addLines (cache.loadFileAsString());
        for (auto& l : lines)
        {
            auto f = juce::StringArray::fromTokens (l, "\t", "");
            if (f.size() < 7 || f[5].isNotEmpty() || f[6] == "1") continue;   // unusable (too new) or a bank
            const juce::File file (f[0]);
            if (! file.existsAsFile() || seen.contains (file.getFullPathName())) continue;
            seen.add (file.getFullPathName());
            Sound s; s.engine = Engine::GhostyPro2; s.factory = false; s.file = file; s.name = f[1]; s.group = f[2];
            s.category = f[4].isNotEmpty() ? f[4] : f[2]; s.id = "gp2:file:" + file.getFullPathName(); s.role = roleOf (s.category + " " + s.name);
            v.push_back (s);
        }
    }
    for (auto& root : vitalFolders())
    {
        juce::Array<juce::File> files; scanFiles (root, "*.vital", 6, files);
        for (auto& file : files)
        {
            if (seen.contains (file.getFullPathName())) continue;
            seen.add (file.getFullPathName());
            auto top = file.getParentDirectory(); while (top.getParentDirectory() != root && top != root && top.getParentDirectory() != top) top = top.getParentDirectory();
            Sound s; s.engine = Engine::GhostyPro2; s.factory = false; s.file = file; s.name = file.getFileNameWithoutExtension();
            s.group = top == root ? root.getFileName() : top.getFileName();
            const auto style = quickVitalStyle (file);
            s.category = style.isNotEmpty() ? style : file.getParentDirectory().getFileName();
            s.id = "gp2:file:" + file.getFullPathName(); s.role = roleOf (s.category + " " + s.name + " " + file.getParentDirectory().getFileName());
            v.push_back (s);
        }
    }
    {
        const auto root = poltergeistUserFolder();
        juce::Array<juce::File> files; scanFiles (root, "*.sxsnp", 6, files);
        for (auto& file : files)
        {
            Sound s; s.engine = Engine::Poltergeist2; s.factory = false; s.file = file; s.name = file.getFileNameWithoutExtension(); s.group = "Yours";
            s.category = file.getParentDirectory() == root ? juce::String ("Yours") : file.getParentDirectory().getFileName();
            s.id = "p2:file:" + file.getFullPathName(); s.role = roleOf (s.category + " " + s.name); v.push_back (s);
        }
    }
    {
        const auto root = atlasPresetFolder();
        juce::Array<juce::File> files; scanFiles (root, "*.atlas", 6, files);
        for (auto& file : files)
        {
            const auto j = juce::JSON::parse (file.loadFileAsString());
            if (! j.isObject()) continue;
            Sound s; s.engine = Engine::Atlas; s.factory = false; s.file = file; s.group = "Yours";
            s.name = j.getProperty ("name", file.getFileNameWithoutExtension()).toString();
            s.category = j.getProperty ("category", "Yours").toString();
            s.id = "atlas:file:" + file.getFullPathName(); s.role = roleOf (s.category); v.push_back (s);
        }
    }
    return v;
}

Catalog& Catalog::get() { static Catalog c; return c; }

Catalog::Catalog()
{
    sounds = factorySounds();
    rescanAsync();
}

Catalog::~Catalog() { if (worker != nullptr) worker->stopThread (5000); }

std::vector<Sound> Catalog::snapshot() const { std::lock_guard<std::mutex> g (lock); return sounds; }

bool Catalog::find (const juce::String& id, Sound& out) const
{
    {
        std::lock_guard<std::mutex> g (lock);
        for (auto& s : sounds) if (s.id == id) { out = s; return true; }
    }
    // not listed (yet): a file id still loads if the file is there
    Engine e;
    if (engineFromKey (id.upToFirstOccurrenceOf (":", false, false), e) && id.fromFirstOccurrenceOf (":", false, false).startsWith ("file:"))
    {
        const juce::File f (id.fromFirstOccurrenceOf (":file:", false, false));
        if (f.existsAsFile()) { out = {}; out.engine = e; out.factory = false; out.file = f; out.name = f.getFileNameWithoutExtension(); out.id = id; out.group = "Yours"; return true; }
    }
    return false;
}

void Catalog::rescanAsync()
{
    if (busy.exchange (true)) return;
    struct Job : juce::Thread
    {
        Catalog& c; explicit Job (Catalog& cc) : juce::Thread ("GHOST sound search"), c (cc) {}
        void run() override
        {
            auto disk = diskSounds();
            auto all = factorySounds();
            all.insert (all.end(), disk.begin(), disk.end());
            { std::lock_guard<std::mutex> g (c.lock); c.sounds = std::move (all); }
            ++c.ver; c.busy = false;
        }
    };
    if (worker != nullptr) worker->stopThread (2000);
    worker = std::make_unique<Job> (*this);
    worker->startThread (juce::Thread::Priority::low);
}

// ---------------------------------------------------------------- states
bool Catalog::nativeState (const Sound& s, juce::MemoryBlock& out, juce::String& error)
{
    out.reset();
    switch (s.engine)
    {
        case Engine::GhostyPro2:
        {   // Ghosty Pro 2's state is Vital's own json, the same text as a .vital file (Vital's setStateInformation reads it)
            if (s.factory)
            {
                const auto& g = gp2Pack();
                if (s.packIndex < 0 || s.packIndex >= (int) g.recs.size()) { error = "missing factory sound"; return false; }
                out.append (g.data + g.recs[(size_t) s.packIndex].at, g.recs[(size_t) s.packIndex].len);
            }
            else if (! s.file.loadFileAsData (out) || out.getSize() < 2) { error = "can't read " + s.file.getFileName(); return false; }
            return true;
        }
        case Engine::Poltergeist2:
        {   // Poltergeist 2's state: the patch text (a .sxsnp file) and its terminating zero; no MORPH / SPECTRAL / SUB after it
            // = those come back off / empty, as when a patch is picked in Poltergeist 2's own browser
            if (s.factory)
            {
                const auto& p = p2Pack();
                if (s.packIndex < 0 || s.packIndex >= (int) p.recs.size()) { error = "missing factory sound"; return false; }
                out.append (p.data + p.recs[(size_t) s.packIndex].at, p.recs[(size_t) s.packIndex].len);
            }
            else if (! s.file.loadFileAsData (out) || out.getSize() < 2) { error = "can't read " + s.file.getFileName(); return false; }
            const char zero = 0; out.append (&zero, 1);
            return true;
        }
        case Engine::Atlas:
        {   // Ghost Atlas's state: {"format":"ghost-atlas-state","version":1,"preset":<the sound>,"presetFile":<where it came from>}
            juce::var preset; juce::String file;
            if (s.factory)
            {
                auto* a = atlasFactory().getArray();
                if (a == nullptr || s.packIndex < 0 || s.packIndex >= a->size()) { error = "missing factory sound"; return false; }
                preset = a->getReference (s.packIndex); file = "factory:" + s.name;
            }
            else { preset = juce::JSON::parse (s.file.loadFileAsString()); file = s.file.getFullPathName(); }
            if (! preset.isObject()) { error = "can't read the sound"; return false; }
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("format", "ghost-atlas-state"); o->setProperty ("version", 1);
            o->setProperty ("preset", preset); o->setProperty ("presetFile", file);
            const auto text = juce::JSON::toString (juce::var (o.get()), true);
            out.append (text.toRawUTF8(), text.getNumBytesAsUTF8());
            return true;
        }
    }
    return false;
}

juce::MemoryBlock Catalog::wrapForVst3Host (const juce::MemoryBlock& componentState)
{
    juce::XmlElement state ("VST3PluginState");
    state.createNewChildElement ("IComponent")->addTextElement (componentState.toBase64Encoding());
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (state, out);
    return out;
}

bool Catalog::hostState (const Sound& s, juce::MemoryBlock& out, juce::String& error)
{
    juce::MemoryBlock raw;
    if (! nativeState (s, raw, error)) return false;
    out = wrapForVst3Host (raw);
    return true;
}
} // namespace ghost
