// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Library.h"
#include "Analysis.h"
#include "Import.h"

namespace atlas
{
// ============================================================================================ JSON
static juce::var floats (const float* v, int n) { juce::Array<juce::var> a; for (int i = 0; i < n; ++i) a.add (std::round ((double) v[i] * 10000.0) / 10000.0); return a; }
static void readFloats (const juce::var& v, float* out, int n) { if (auto* a = v.getArray()) for (int i = 0; i < n && i < a->size(); ++i) out[i] = (float) (double) a->getReference (i); }

juce::var Features::toVar() const
{
    auto o = newObject();
    o->setProperty ("valid", valid); o->setProperty ("loudness", loudnessDb); o->setProperty ("peak", peakDb);
    o->setProperty ("attackMs", attackMs); o->setProperty ("sustain", sustain); o->setProperty ("releaseMs", releaseMs);
    o->setProperty ("centroid", centroidHz); o->setProperty ("brightness", brightness);
    o->setProperty ("low", low); o->setProperty ("mid", mid); o->setProperty ("high", high); o->setProperty ("air", air);
    o->setProperty ("flatness", flatness); o->setProperty ("harmonicity", harmonicity); o->setProperty ("ring", ring);
    o->setProperty ("tune", tuneCents); o->setProperty ("pitchSpread", pitchSpread);
    o->setProperty ("width", width); o->setProperty ("monoLoss", monoLossDb); o->setProperty ("movement", movement);
    o->setProperty ("loopClick", loopClick); o->setProperty ("transient", transient); o->setProperty ("velocityDb", velocityDb);
    o->setProperty ("probe", probeNote);
    o->setProperty ("bands", floats (bands.data(), (int) bands.size()));
    o->setProperty ("mfcc", floats (mfcc.data(), (int) mfcc.size()));
    o->setProperty ("mfccDev", floats (mfccDev.data(), (int) mfccDev.size()));
    o->setProperty ("fft", floats (fft.data(), (int) fft.size()));
    return o.get();
}
Features Features::fromVar (const juce::var& v)
{
    Features f;
    if (! v.isObject()) return f;
    f.valid = (bool) prop (v, "valid", false);
    f.loudnessDb = (float) (double) prop (v, "loudness", -30.0); f.peakDb = (float) (double) prop (v, "peak", -12.0);
    f.attackMs = (float) (double) prop (v, "attackMs", 5.0); f.sustain = (float) (double) prop (v, "sustain", 1.0); f.releaseMs = (float) (double) prop (v, "releaseMs", 200.0);
    f.centroidHz = (float) (double) prop (v, "centroid", 1000.0); f.brightness = (float) (double) prop (v, "brightness", 0.5);
    f.low = (float) (double) prop (v, "low", 0.2); f.mid = (float) (double) prop (v, "mid", 0.5); f.high = (float) (double) prop (v, "high", 0.25); f.air = (float) (double) prop (v, "air", 0.05);
    f.flatness = (float) (double) prop (v, "flatness", 0.1); f.harmonicity = (float) (double) prop (v, "harmonicity", 0.8); f.ring = (float) (double) prop (v, "ring", 0.0);
    f.tuneCents = (float) (double) prop (v, "tune", 0.0); f.pitchSpread = (float) (double) prop (v, "pitchSpread", 0.0);
    f.width = (float) (double) prop (v, "width", 0.2); f.monoLossDb = (float) (double) prop (v, "monoLoss", 0.0); f.movement = (float) (double) prop (v, "movement", 0.1);
    f.loopClick = (float) (double) prop (v, "loopClick", 0.0); f.transient = (float) (double) prop (v, "transient", 0.0); f.velocityDb = (float) (double) prop (v, "velocityDb", 12.0);
    f.probeNote = (int) prop (v, "probe", 60);
    readFloats (prop (v, "bands"), f.bands.data(), (int) f.bands.size());
    readFloats (prop (v, "mfcc"), f.mfcc.data(), (int) f.mfcc.size());
    readFloats (prop (v, "mfccDev"), f.mfccDev.data(), (int) f.mfccDev.size());
    readFloats (prop (v, "fft"), f.fft.data(), (int) f.fft.size());
    return f;
}

juce::var Instrument::toVar() const
{
    auto o = newObject();
    o->setProperty ("id", id); o->setProperty ("name", name); o->setProperty ("library", libraryId); o->setProperty ("libraryName", libraryName);
    o->setProperty ("sfz", sfz); o->setProperty ("format", format); o->setProperty ("category", category); o->setProperty ("tags", fromStrings (tags));
    o->setProperty ("author", author); o->setProperty ("license", license); o->setProperty ("source", source); o->setProperty ("restrictions", restrictions);
    o->setProperty ("regions", regions); o->setProperty ("samples", samples); o->setProperty ("loKey", loKey); o->setProperty ("hiKey", hiKey);
    if (keyMap.isNotEmpty()) o->setProperty ("keys", keyMap);
    o->setProperty ("velocityLayers", velocityLayers); o->setProperty ("roundRobins", roundRobins); o->setProperty ("looped", looped); o->setProperty ("releaseSamples", hasReleaseSamples);
    o->setProperty ("bytes", bytes); o->setProperty ("added", added);
    o->setProperty ("unsupported", fromStrings (unsupported)); o->setProperty ("notes", fromStrings (notes));
    o->setProperty ("features", f.toVar());
    o->setProperty ("preview", floats (preview.data(), (int) preview.size()));
    o->setProperty ("favourite", favourite); o->setProperty ("hidden", hidden);
    return o.get();
}
Instrument Instrument::fromVar (const juce::var& v)
{
    Instrument i;
    i.id = prop (v, "id").toString(); i.name = prop (v, "name").toString(); i.libraryId = prop (v, "library").toString(); i.libraryName = prop (v, "libraryName").toString();
    i.sfz = prop (v, "sfz").toString(); i.format = prop (v, "format", "sfz").toString(); i.category = prop (v, "category").toString(); i.tags = toStrings (prop (v, "tags"));
    i.author = prop (v, "author").toString(); i.license = prop (v, "license").toString(); i.source = prop (v, "source").toString(); i.restrictions = prop (v, "restrictions").toString();
    i.regions = (int) prop (v, "regions", 0); i.samples = (int) prop (v, "samples", 0); i.loKey = (int) prop (v, "loKey", 0); i.hiKey = (int) prop (v, "hiKey", 127);
    i.keyMap = prop (v, "keys").toString();
    i.velocityLayers = (int) prop (v, "velocityLayers", 1); i.roundRobins = (int) prop (v, "roundRobins", 1); i.looped = (bool) prop (v, "looped", false); i.hasReleaseSamples = (bool) prop (v, "releaseSamples", false);
    i.bytes = (juce::int64) prop (v, "bytes", 0); i.added = (juce::int64) prop (v, "added", 0);
    i.unsupported = toStrings (prop (v, "unsupported")); i.notes = toStrings (prop (v, "notes"));
    i.f = Features::fromVar (prop (v, "features"));
    if (auto* a = prop (v, "preview").getArray()) for (const auto& e : *a) i.preview.push_back ((float) (double) e);
    i.favourite = (bool) prop (v, "favourite", false); i.hidden = (bool) prop (v, "hidden", false);
    return i;
}

juce::var LibrarySource::toVar() const
{
    auto o = newObject();
    o->setProperty ("id", id); o->setProperty ("name", name); o->setProperty ("folder", folder); o->setProperty ("author", author); o->setProperty ("credit", credit);
    o->setProperty ("license", license); o->setProperty ("licenseUrl", licenseUrl); o->setProperty ("source", source); o->setProperty ("restrictions", restrictions);
    o->setProperty ("version", version); o->setProperty ("signature", signature); o->setProperty ("external", external);
    if (record.isNotEmpty()) o->setProperty ("record", record);
    if (category.isNotEmpty()) o->setProperty ("category", category);
    o->setProperty ("bytes", bytes); o->setProperty ("added", added); o->setProperty ("instruments", instruments);
    return o.get();
}
LibrarySource LibrarySource::fromVar (const juce::var& v)
{
    LibrarySource s;
    s.id = prop (v, "id").toString(); s.name = prop (v, "name").toString(); s.folder = prop (v, "folder").toString(); s.author = prop (v, "author").toString(); s.credit = prop (v, "credit").toString();
    s.license = prop (v, "license").toString(); s.licenseUrl = prop (v, "licenseUrl").toString(); s.source = prop (v, "source").toString(); s.restrictions = prop (v, "restrictions").toString();
    s.version = prop (v, "version").toString(); s.signature = prop (v, "signature").toString(); s.external = (bool) prop (v, "external", false);
    s.record = prop (v, "record").toString();
    s.category = prop (v, "category").toString();
    s.bytes = (juce::int64) prop (v, "bytes", 0); s.added = (juce::int64) prop (v, "added", 0); s.instruments = (int) prop (v, "instruments", 0);
    return s;
}

// ============================================================================================ which keys play
juce::String keysToMap (const std::vector<int>& keys)
{
    juce::uint32 w[4] = {};
    for (int k : keys) if (k >= 0 && k < 128) w[k >> 5] |= 1u << (k & 31);
    juce::String s;
    for (auto v : w) s << juce::String::toHexString ((int) v).paddedLeft ('0', 8);
    return s;
}
bool Instrument::plays (int key) const
{
    if (key < 0 || key > 127) return false;
    if (keyMap.length() != 32) return key >= loKey && key <= hiKey;
    return ((juce::uint32) keyMap.substring ((key >> 5) * 8, (key >> 5) * 8 + 8).getHexValue32() >> (key & 31)) & 1u;
}
int Instrument::nearestKey (int key) const
{
    for (int d = 0; d < 128; ++d) { if (plays (key - d)) return key - d; if (plays (key + d)) return key + d; }
    return juce::jlimit (loKey, hiKey, key);
}

// ============================================================================================ the database
bool Library::load()
{
    const auto v = readJson (databaseFile());
    if (! v.isObject()) return false;
    std::vector<Instrument> in; std::vector<LibrarySource> ls;
    if (auto* a = prop (v, "instruments").getArray()) for (const auto& e : *a) in.push_back (Instrument::fromVar (e));
    if (auto* a = prop (v, "libraries").getArray()) for (const auto& e : *a) ls.push_back (LibrarySource::fromVar (e));
    const juce::ScopedLock sl (lock);
    items = std::move (in); libs = std::move (ls);
    return true;
}
bool Library::save() const
{
    auto o = newObject();
    juce::Array<juce::var> ia, la;
    {
        const juce::ScopedLock sl (lock);
        for (const auto& i : items) ia.add (i.toVar());
        for (const auto& l : libs) la.add (l.toVar());
    }
    o->setProperty ("version", 1); o->setProperty ("libraries", la); o->setProperty ("instruments", ia);
    return writeJson (databaseFile(), o.get());
}
std::vector<Instrument> Library::all() const { const juce::ScopedLock sl (lock); return items; }
int Library::size() const { const juce::ScopedLock sl (lock); return (int) items.size(); }
std::vector<LibrarySource> Library::sources() const { const juce::ScopedLock sl (lock); return libs; }
int Library::indexOfLocked (const juce::String& id) const { for (size_t i = 0; i < items.size(); ++i) if (items[i].id == id) return (int) i; return -1; }
bool Library::find (const juce::String& id, Instrument& out) const
{
    const juce::ScopedLock sl (lock);
    const int i = indexOfLocked (id); if (i < 0) return false; out = items[(size_t) i]; return true;
}
bool Library::findLoose (const juce::String& id, Instrument& out) const
{
    if (find (id, out)) return true;
    const auto lib = id.upToFirstOccurrenceOf (":", false, false), leaf = id.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf (":", false, false);
    const auto rest = id.fromFirstOccurrenceOf (":", false, false);
    const juce::ScopedLock sl (lock);
    for (const auto& i : items)      // "<library>:<the instrument's name>": how the factory sounds name instruments (it survives a library's files being renamed)
        if (i.libraryId == lib && i.name.equalsIgnoreCase (rest)) { out = i; return true; }
    for (const auto& i : items)
        if (i.libraryId == lib && i.id.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf (":", false, false).equalsIgnoreCase (leaf)) { out = i; return true; }
    return false;
}
juce::File Library::folderOf (const LibrarySource& s) const { return s.external ? juce::File (s.folder) : libraryDir().getChildFile (s.folder); }
juce::File Library::sfzFile (const Instrument& inst) const
{
    const juce::ScopedLock sl (lock);
    for (const auto& l : libs) if (l.id == inst.libraryId) return folderOf (l).getChildFile (inst.sfz);
    return libraryDir().getChildFile (inst.libraryId).getChildFile (inst.sfz);
}
juce::String Library::sfzPath (const juce::String& id) const
{
    Instrument i; if (! findLoose (id, i)) return {};
    const auto f = sfzFile (i);
    return f.existsAsFile() ? f.getFullPathName() : juce::String();
}
void Library::setFavourite (const juce::String& id, bool on) { const juce::ScopedLock sl (lock); const int i = indexOfLocked (id); if (i >= 0) items[(size_t) i].favourite = on; }
void Library::setCategory (const juce::String& id, const juce::String& c) { const juce::ScopedLock sl (lock); const int i = indexOfLocked (id); if (i >= 0) items[(size_t) i].category = c; }
void Library::put (const Instrument& inst)
{
    const juce::ScopedLock sl (lock);
    const int i = indexOfLocked (inst.id);
    if (i >= 0) { const bool fav = items[(size_t) i].favourite; items[(size_t) i] = inst; items[(size_t) i].favourite = fav; }
    else items.push_back (inst);
}
void Library::remove (const juce::String& id) { const juce::ScopedLock sl (lock); const int i = indexOfLocked (id); if (i >= 0) items.erase (items.begin() + i); }
bool Library::relink (const juce::String& libraryId, const juce::File& newFolder)
{
    if (! newFolder.isDirectory()) return false;
    const juce::ScopedLock sl (lock);
    for (auto& l : libs)
        if (l.id == libraryId)
        {
            if (newFolder.isAChildOf (libraryDir())) { l.external = false; l.folder = newFolder.getRelativePathFrom (libraryDir()); }
            else { l.external = true; l.folder = newFolder.getFullPathName(); }
            return true;
        }
    return false;
}
juce::StringArray Library::missing() const
{
    juce::StringArray out;
    for (const auto& i : all()) if (! sfzFile (i).existsAsFile()) out.add (i.id);
    return out;
}

std::vector<Library::Mapped> Library::mappingOf (const Instrument& inst) const
{
    std::vector<Mapped> out;
    if (inst.format != "mapped") return out;
    LibrarySource src; bool have = false;
    for (const auto& l : sources()) if (l.id == inst.libraryId) { src = l; have = true; }
    const auto sfz = sfzFile (inst);
    if (! have || ! sfz.existsAsFile()) return out;
    const auto folder = folderOf (src);
    // (the SFZ is one this importer wrote: a default_path, then groups that name the key and regions that name the recording)
    juce::StringArray lines; lines.addLines (sfz.loadFileAsString());
    juce::File dir = sfz.getParentDirectory();
    int key = 60;
    for (const auto& line : lines)
    {
        if (line.startsWith ("<control>") && line.contains ("default_path=")) dir = sfz.getParentDirectory().getChildFile (line.fromFirstOccurrenceOf ("default_path=", false, false).trim());
        else if (line.startsWith ("<group>") && line.contains ("pitch_keycenter=")) key = line.fromFirstOccurrenceOf ("pitch_keycenter=", false, false).upToFirstOccurrenceOf (" ", false, false).getIntValue();
        else if (line.startsWith ("<region>") && line.contains ("sample="))
        {
            auto name = line.fromFirstOccurrenceOf ("sample=", false, false);
            for (const auto* stop : { " seq_position=", " loop_mode=" }) name = name.upToFirstOccurrenceOf (stop, false, false);
            out.push_back ({ dir.getChildFile (name.trim()).getRelativePathFrom (folder).replaceCharacter ('\\', '/'), key });
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const Mapped& a, const Mapped& b) { return a.note != b.note ? a.note < b.note : a.file.compareNatural (b.file) < 0; });
    return out;
}

ImportReport Library::remap (const Instrument& inst, const std::vector<Mapped>& changed, const Progress& progress)
{
    ImportReport rep;
    LibrarySource src; bool have = false;
    for (const auto& l : sources()) if (l.id == inst.libraryId) { src = l; have = true; }
    if (! have || changed.empty()) { rep.lines.add ("Nothing was changed."); return rep; }
    auto fixed = importer::readMapping (src.id);
    for (const auto& c : changed) fixed[c.file] = juce::jlimit (0, 127, c.note);
    if (! importer::writeMapping (src.id, fixed)) { rep.failed = 1; rep.lines.add ("The new mapping could not be written to " + mappingsDir().getFullPathName()); return rep; }
    rep = indexFolder (src, folderOf (src), progress, 0.0f, 1.0f, true);
    save();
    return rep;
}

juce::String Library::manifestText() const
{
    juce::String t;
    t << "GHOST ATLAS - where every sound comes from\n\n";
    for (const auto& l : sources())
    {
        t << l.name << "\n  id: " << l.id << "\n  author: " << (l.author.isEmpty() ? "unknown" : l.author) << "\n  licence: " << (l.license.isEmpty() ? "not stated - check before sharing anything made with it" : l.license);
        if (l.licenseUrl.isNotEmpty()) t << "  (" << l.licenseUrl << ")";
        t << "\n  source: " << (l.source.isEmpty() ? "imported from this computer" : l.source) << "\n  restrictions: " << (l.restrictions.isEmpty() ? "none recorded" : l.restrictions)
          << "\n  on disk: " << folderOf (l).getFullPathName() << "\n  instruments: " << l.instruments << ", " << juce::File::descriptionOfSizeInBytes (l.bytes) << "\n";
        if (l.credit.isNotEmpty()) t << "  credit: " << l.credit << "\n";
        t << "\n";
    }
    return t;
}

// ============================================================================================ names -> category, tags
namespace
{
juce::StringArray wordsOf (const juce::String& s)
{
    juce::StringArray t;
    t.addTokens (s.toLowerCase().replaceCharacters ("_-./\\()[],+&#'", "               "), " ", "");
    t.removeEmptyStrings();
    return t;
}
// a pattern ending in * matches the start of a word ("choir*" -> choir, choirs); anything else is the whole word
bool anyWord (const juce::StringArray& words, std::initializer_list<const char*> patterns)
{
    for (const auto& w : words)
        for (const char* p : patterns)
        {
            const juce::String pat (p);
            if (pat.endsWithChar ('*') ? w.startsWith (pat.dropLastCharacters (1)) : w == pat) return true;
        }
    return false;
}
juce::String categoryFromWords (const juce::StringArray& w)
{
    if (w.isEmpty()) return {};
    const bool wind = anyWord (w, { "horn*", "trumpet*", "trombone*", "tuba", "flute*", "oboe*", "clarinet*", "bassoon*", "contrabassoon*", "sax*", "saxophone*", "recorder", "ocarina", "piccolo", "anglais", "cornet*", "euphonium" });
    if (wind && ! anyWord (w, { "synth*" })) return "Cinematic";
    if (anyWord (w, { "choir*", "chorus", "ork", "orks", "aah*", "ooh*", "vox", "voice*", "vocal*", "sing", "singer*", "singing", "chant*", "soprano*", "alto", "tenor*", "humming", "whisper*" }))
    {
        if (anyWord (w, { "chop*", "phrase*", "dialogue", "word*", "shout*", "hit", "hits" })) return "Vocal Chops";
        if (anyWord (w, { "texture*", "breath*", "whisper*", "predrop*", "fx", "noise*" })) return "Vocal Textures";
        return anyWord (w, { "pad*" }) ? "Vocal Pads" : "Choirs";
    }
    const bool electric = anyWord (w, { "fm", "electric", "ep", "epiano*", "rhodes", "wurli*", "synthesized", "pianet", "cp", "cp80", "dx", "dx7", "tx81z", "e" });
    if (anyWord (w, { "piano*", "grand", "upright", "steinway", "bosendorfer" }) && ! electric) return "Pianos";
    if (electric || anyWord (w, { "piano*", "clav*", "organ*", "harpsichord", "celest*", "accordion", "harmonium", "mellotron", "melodica", "keys" })) return "Keys";
    if (anyWord (w, { "bell*", "glock*", "vibra*", "vibes", "chime*", "kalimba", "mbira", "marimba", "xylo*", "hang", "handpan", "crotale*", "glass*", "bowl*", "musicbox", "balafon", "gong*", "triangle", "tubular", "tine*" })) return "Bells";
    if (anyWord (w, { "cyborg*" })) return "Hybrid Instruments";
    if (anyWord (w, { "synth*" }) && anyWord (w, { "string*" })) return "Pads";
    if (anyWord (w, { "pizz*", "harp", "guitar*", "koto", "ukulele", "banjo", "pluck*", "strumstick", "dulcimer", "lute", "sitar", "mandolin*", "zither" })) return "Plucks";
    if (anyWord (w, { "violin*", "viola*", "cello*", "celli", "contrabass*", "string*", "basses", "psaltery", "fiddle", "erhu" })) return "Strings";
    if (anyWord (w, { "pad*", "sweep*", "newage", "atmos*" }) || (w.contains ("new") && w.contains ("age"))) return "Pads";
    if (anyWord (w, { "soundtrack", "scifi", "sci", "goblin*", "ocean", "rain", "wind", "drone*", "texture*", "soundscape*", "ambience" })) return "Soundscapes";
    if (anyWord (w, { "lead*", "square", "saw", "calliope", "fifths" })) return "Leads";
    if (anyWord (w, { "analog*", "moog", "juno", "minilogue", "prophet", "jupiter", "cowsynth", "caveman", "oberheim", "lately" }) || (anyWord (w, { "synth*" }) && anyWord (w, { "bass", "brass" }))) return "Analog Synths";
    if (anyWord (w, { "akwf", "wave*", "digital", "wavestate", "minifreak", "wavetable" })) return "Digital Synths";
    if (anyWord (w, { "scary*", "weird", "didgeridoo", "noise*", "siren", "experimental", "glitch*", "prepared" })) return "Experimental";
    if (anyWord (w, { "horn*", "trumpet*", "trombone*", "tuba", "flute*", "oboe*", "clarinet*", "bassoon*", "sax*", "recorder", "ocarina", "piccolo", "brass", "timpani", "cymbal*", "drum*", "perc*", "orchestra*", "ensemble", "taiko", "weresax" })) return "Cinematic";
    return {};
}
} // namespace

// the instrument's own name decides first; if it says nothing, what its library's record says its sounds are; then
// where it came from (the library's name, its folders)
juce::String guessCategory (const juce::String& name, const juce::String& context, const juce::String& libraryCategory)
{
    auto c = categoryFromWords (wordsOf (name));
    if (c.isEmpty()) c = libraryCategory;
    if (c.isEmpty()) c = categoryFromWords (wordsOf (context));
    return c.isEmpty() ? juce::String ("Hybrid Instruments") : c;
}

// what was heard can correct what a name suggested: spoken phrases are chops, not a choir
juce::String refineCategory (const juce::String& category, const Features& f)
{
    if (! f.valid) return category;
    if (category == "Choirs" || category == "Vocal Pads")
    {
        if (f.sustain < 0.15f && f.harmonicity < 0.5f) return "Vocal Chops";
        if (f.harmonicity < 0.4f && f.movement > 0.6f) return "Vocal Textures";
    }
    return category;
}

juce::StringArray guessTags (const juce::String& name, const Features& f)
{
    juce::StringArray t;
    if (! f.valid) return t;
    // sound character
    if (f.brightness > 0.62f) t.add ("bright"); else if (f.brightness < 0.3f) t.add ("dark"); else t.add ("warm");
    if (f.air > 0.004f) t.add ("airy");
    if (f.low > 0.45f) t.add ("deep");
    if (f.flatness > 0.45f) t.add ("breathy");
    if (f.width > 0.25f) t.add ("wide");
    if (f.movement > 0.35f) t.add ("evolving"); else if (f.sustain > 0.6f) t.add ("steady");
    if (f.sustain > 0.6f && f.attackMs > 120.0f) t.add ("slow");
    if (f.sustain < 0.2f) t.add (f.releaseMs < 400.0f ? "short" : "ringing");
    if (f.transient > 0.5f) t.add ("percussive");
    if (f.harmonicity < 0.45f) t.add ("noisy");
    // mood, from the same numbers
    if (f.brightness < 0.35f && f.sustain > 0.5f) t.add ("moody");
    if (f.brightness > 0.5f && f.air > 0.004f && f.sustain > 0.4f) t.add ("ethereal");
    if (f.brightness >= 0.3f && f.brightness <= 0.6f && f.flatness < 0.2f && f.transient < 0.5f) t.add ("gentle");
    juce::ignoreUnused (name);
    return t;
}

// ============================================================================================ indexing
ImportReport Library::importFolder (const juce::File& folder, bool copyIn, const Progress& progress)
{
    ImportReport rep;
    if (! folder.isDirectory()) { rep.failed = 1; rep.lines.add ("Not a folder: " + folder.getFullPathName()); return rep; }

    LibrarySource src;
    src.name = folder.getFileName();
    src.id = safeName (folder.getFileName()).toLowerCase().replaceCharacter (' ', '-').retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-_");
    if (src.id.isEmpty()) src.id = "library";
    src.signature = importer::folderSignature (folder);
    src.added = juce::Time::currentTimeMillis();

    // the same library dropped twice
    for (const auto& l : sources())
        if (l.signature == src.signature && src.signature.isNotEmpty() && folderOf (l).isDirectory())
        {
            rep.duplicates = 1;
            rep.lines.add ("\"" + folder.getFileName() + "\" is already in the library as \"" + l.name + "\" (same files, same sizes). Nothing was copied.");
            return rep;
        }
    // an id of its own
    { int n = 2; const auto base = src.id; auto taken = [this] (const juce::String& id) { for (const auto& l : sources()) if (l.id == id) return true; return libraryDir().getChildFile (id).exists(); };
      while (taken (src.id)) src.id = base + "-" + juce::String (n++); }

    juce::File home = folder;
    if (folder.isAChildOf (libraryDir())) { src.folder = folder.getRelativePathFrom (libraryDir()); }
    else if (copyIn)
    {
        home = libraryDir().getChildFile (src.id);
        if (progress) progress (0.02f, "Copying " + folder.getFileName());
        if (! importer::copyFolder (folder, home, [&progress] (float f, const juce::String& w) { return progress ? progress (0.02f + 0.28f * f, w) : true; }))
        { rep.failed = 1; rep.lines.add ("Could not copy \"" + folder.getFileName() + "\" into the library (is the disk full?)."); home.deleteRecursively(); return rep; }
        src.folder = src.id;
        rep.lines.add ("Copied \"" + folder.getFileName() + "\" into the library. The original folder was not changed.");
    }
    else { src.external = true; src.folder = folder.getFullPathName(); rep.lines.add ("Using \"" + folder.getFileName() + "\" where it is (not copied)."); }

    auto r = indexFolder (src, home, progress, copyIn ? 0.3f : 0.02f, 1.0f, true);
    rep.lines.addArray (r.lines); rep.added += r.added; rep.updated += r.updated; rep.failed += r.failed; rep.unmapped += r.unmapped; rep.instrumentIds.addArray (r.instrumentIds);
    save();
    return rep;
}

static constexpr int kImporterVersion = 7;

ImportReport Library::indexManagedLibrary (const Progress& progress, bool analyse)
{
    ImportReport rep;
    juce::Array<juce::File> folders;
    libraryDir().findChildFiles (folders, juce::File::findDirectories, false);
    folders.sort();
    int k = 0;
    for (const auto& f : folders)
    {
        if (f.getFileName().startsWith (".") || f.getFileName().endsWith (".incoming")) { ++k; continue; }
        LibrarySource src;
        src.id = f.getFileName(); src.name = f.getFileName(); src.folder = f.getFileName();
        src.signature = importer::folderSignature (f); src.added = juce::Time::currentTimeMillis();
        {
            const auto rec = f.getChildFile ("ATLAS-SOURCE.json");
            // (the importer's own version is part of it: when the way libraries are read improves, each is read again once)
            src.record = juce::String::toHexString ((rec.existsAsFile() ? rec.loadFileAsString() : juce::String()).hashCode64()) + "-i" + juce::String (kImporterVersion);
        }
        bool known = false;
        for (const auto& l : sources()) if (l.id == src.id && l.signature == src.signature && l.record == src.record) known = true;
        const float p0 = (float) k / (float) juce::jmax (1, folders.size()), p1 = (float) (k + 1) / (float) juce::jmax (1, folders.size());
        ++k;
        if (known) { if (progress) progress (p1, src.name + " (already indexed)"); continue; }
        auto r = indexFolder (src, f, progress, p0, p1, analyse);
        rep.lines.addArray (r.lines); rep.added += r.added; rep.updated += r.updated; rep.failed += r.failed; rep.unmapped += r.unmapped; rep.instrumentIds.addArray (r.instrumentIds);
    }
    // a managed library whose folder is gone takes its instruments with it (one linked from elsewhere is kept: it can be found again)
    {
        std::vector<LibrarySource> gone;
        for (const auto& l : sources()) if (! l.external && ! folderOf (l).isDirectory()) gone.push_back (l);
        for (const auto& l : gone)
        {
            std::vector<juce::String> ids;
            for (const auto& i : all()) if (i.libraryId == l.id) ids.push_back (i.id);
            for (const auto& id : ids) remove (id);
            { const juce::ScopedLock sl (lock); libs.erase (std::remove_if (libs.begin(), libs.end(), [&l] (const LibrarySource& s) { return s.id == l.id; }), libs.end()); }
            rep.lines.add (l.name + ": its folder is no longer in the library (" + juce::String ((int) ids.size()) + " instruments taken off the list)");
        }
    }
    save();
    return rep;
}

// one thing a folder can become (its SFZ, written or found): scanned, listened to, and put in the library
bool Library::addFound (const LibrarySource& src, const juce::File& folder, const importer::Found& fd, bool analyse, ImportReport& rep, juce::int64& totalBytes)
{
    const auto scan = SfzLayer::scan (fd.sfz.getFullPathName().toStdString());
    if (! scan.ok) { ++rep.failed; rep.lines.add ("  could not read " + fd.sfz.getFileName() + ": " + juce::String (scan.error)); return false; }
    if ((int) scan.missing.size() >= juce::jmax (1, (int) scan.samples.size()) && scan.oscillatorRegions == 0)
    { ++rep.failed; rep.lines.add ("  " + fd.name + ": none of its " + juce::String ((int) scan.samples.size()) + " samples were found on disk (skipped)"); return false; }

    Instrument inst;
    inst.libraryId = src.id; inst.libraryName = src.name;
    inst.sfz = fd.sfz.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
    inst.id = src.id + ":" + inst.sfz.upToLastOccurrenceOf (".", false, false);
    inst.name = fd.name; inst.format = fd.format;
    inst.author = src.author; inst.license = src.license; inst.source = src.source; inst.restrictions = src.restrictions;
    inst.regions = scan.regions; inst.samples = (int) scan.samples.size(); inst.loKey = scan.loKey; inst.hiKey = scan.hiKey;
    inst.velocityLayers = scan.velocityLayers; inst.roundRobins = scan.roundRobins; inst.looped = scan.loopedRegions > 0; inst.hasReleaseSamples = scan.releaseRegions > 0;
    if ((int) scan.keys.size() < scan.hiKey - scan.loKey + 1) inst.keyMap = keysToMap (scan.keys);       // it has gaps: remember which keys play
    inst.bytes = scan.sampleBytes; inst.added = juce::Time::currentTimeMillis();
    inst.notes = fd.notes;
    if (! scan.missing.empty()) inst.notes.add (juce::String ((int) scan.missing.size()) + " of its samples are missing");
    for (const auto& e : scan.parseErrors) inst.notes.addIfNotAlreadyThere ("SFZ error: " + juce::String (e));
    inst.category = fd.category.isNotEmpty() ? fd.category
                                             : guessCategory (inst.name, src.name + " " + inst.sfz, src.category);
    totalBytes += scan.sampleBytes;

    Instrument old;
    const bool existed = find (inst.id, old);
    if (existed) { inst.favourite = old.favourite; if (old.category.isNotEmpty() && fd.category.isEmpty()) inst.category = old.category; }

    if (analyse)
    {
        const auto a = analyseInstrument (fd.sfz, inst.id, scan.keys);
        inst.unsupported = a.unsupported;
        if (a.ok)
        {
            inst.f = a.features; inst.preview = a.preview; inst.tags = guessTags (inst.name, inst.f); if (a.regions > 0) inst.regions = a.regions;
            // an oscillator is on its key by construction (voices set slightly apart beat, and the beating misleads the reading)
            if (fd.format == "wave") { inst.f.tuneCents = 0.0f; inst.f.pitchSpread = 0.0f; }
            if (! (existed && old.category.isNotEmpty())) inst.category = refineCategory (inst.category, inst.f);
        }
        else { ++rep.failed; rep.lines.add ("  " + inst.name + ": " + a.error + " (not added)"); return false; }
        if (! inst.unsupported.isEmpty())
            rep.lines.add ("  " + inst.name + ": " + juce::String (inst.unsupported.size()) + " SFZ setting" + (inst.unsupported.size() == 1 ? "" : "s") + " the engine does not play: " + inst.unsupported.joinIntoString (", ").substring (0, 300));
    }
    put (inst);
    rep.instrumentIds.add (inst.id);
    if (existed) ++rep.updated; else ++rep.added;
    return true;
}

// an instrument made here (REIMAGINE writes them): its SFZ lies in the library's own folder under Library/
bool Library::addMade (LibrarySource src, const juce::File& sfz, const juce::String& name, const juce::String& category, const juce::StringArray& notes, Instrument& out, juce::String& error)
{
    const auto folder = folderOf (src);
    importer::Found fd; fd.sfz = sfz; fd.name = name; fd.format = "made"; fd.category = category; fd.notes = notes;
    ImportReport rep; juce::int64 bytes = 0;
    if (! addFound (src, folder, fd, true, rep, bytes) || rep.instrumentIds.isEmpty()) { error = rep.lines.joinIntoString ("; ").trim(); return false; }
    {
        const juce::ScopedLock sl (lock);
        bool have = false;
        for (auto& l : libs) if (l.id == src.id) { l.bytes += bytes; ++l.instruments; have = true; }
        if (! have) { src.bytes = bytes; src.instruments = 1; src.added = juce::Time::currentTimeMillis(); libs.push_back (src); }
    }
    save();
    return find (rep.instrumentIds[0], out);
}

ImportReport Library::indexFolder (LibrarySource src, const juce::File& folder, const Progress& progress, float p0, float p1, bool analyse)
{
    ImportReport rep;
    auto say = [&] (float f, const juce::String& w) { return progress ? progress (p0 + (p1 - p0) * f, w) : true; };

    // where it came from: what fetch_library.py wrote beside the files, else whatever licence text the folder holds
    importer::readSourceRecord (folder, src);
    say (0.0f, "Reading " + src.name);

    // 1. everything the folder can become: its own SFZ files, SoundFonts and Decent Sampler packs (converted to SFZ beside
    //    the originals, in _atlas/), and loose samples (mapped from their names)
    std::vector<importer::Found> found = importer::discover (folder, src, rep, [&] (float f, const juce::String& w) { return say (0.35f * f, w); });

    // 2. each one becomes an instrument; then it is listened to
    juce::int64 totalBytes = 0;
    int n = 0;
    for (const auto& fd : found)
    {
        const float fr = 0.35f + 0.65f * (float) n / (float) std::max<size_t> (1, found.size());
        ++n;
        if (! say (fr, fd.name)) { rep.lines.add ("Stopped."); break; }
        if (! addFound (src, folder, fd, analyse, rep, totalBytes)) continue;
    }

    // what this library no longer yields (left out by its record now, renamed, or gone from disk) leaves the list
    {
        std::vector<juce::String> gone;
        for (const auto& i : all()) if (i.libraryId == src.id && ! rep.instrumentIds.contains (i.id)) gone.push_back (i.id);
        for (const auto& id : gone) remove (id);
        if (! gone.empty()) rep.lines.add ("  " + juce::String ((int) gone.size()) + " earlier entries of this library are no longer there (taken off the list)");
    }
    src.bytes = totalBytes; src.instruments = rep.added + rep.updated;
    {
        const juce::ScopedLock sl (lock);
        bool replaced = false;
        for (auto& l : libs) if (l.id == src.id) { l = src; replaced = true; }
        if (! replaced) libs.push_back (src);
    }
    rep.lines.insert (0, src.name + ": " + juce::String (rep.added + rep.updated) + " instrument" + ((rep.added + rep.updated) == 1 ? "" : "s")
                         + (rep.failed > 0 ? ", " + juce::String (rep.failed) + " could not be used" : juce::String())
                         + (rep.unmapped > 0 ? ", " + juce::String (rep.unmapped) + (rep.unmapped == 1 ? " loose folder mapped in file order" : " loose folders mapped in file order") : juce::String())
                         + "  [" + (src.license.isEmpty() ? juce::String ("licence not stated") : src.license) + "]");
    say (1.0f, src.name);
    return rep;
}

int Library::analyseMissing (const Progress& progress, bool everything)
{
    const auto list = all();
    int done = 0, k = 0;
    for (const auto& i : list)
    {
        ++k;
        if (i.f.valid && ! everything) continue;
        if (progress && ! progress ((float) k / (float) std::max<size_t> (1, list.size()), i.name)) break;
        const auto f = sfzFile (i);
        if (! f.existsAsFile()) continue;
        const auto a = analyseInstrument (f, i.id, SfzLayer::scan (f.getFullPathName().toStdString(), false).keys);
        if (! a.ok) continue;
        Instrument u = i; u.f = a.features; u.preview = a.preview; u.unsupported = a.unsupported; u.tags = guessTags (u.name, u.f);
        put (u); ++done;
    }
    if (done > 0) save();
    return done;
}
} // namespace atlas
