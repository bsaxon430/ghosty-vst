#pragma once
// MY BANKS (owner 2026-10-30): the user's own drum banks, ONE library shared by every GhostBlock drum plugin (Block Step, Ghost House,
// and Ghost Kit next). Lives in  ~/Music/GhostBlock Banks/ :
//     library.json        {"v":1,"nb":<next bank number>,"nu":<next sound id>,"banks":[{"id":"my1","name":"808 KIT","s":[uid,...]}],
//                          "snd":{"<uid>":{"n":"Kick 1","c":"kick","f":"sounds/12-Kick 1.wav"}}}
//     sounds/             a COPY of every sound added (moving or deleting the originals never breaks a bank)
// A sound keeps its id (uid) forever, so a beat that uses it keeps it even when banks are renamed or sounds are re-typed.
// The page addresses a user sound as  s = kBase + uid  (any drum), the engines map that index to the file.
// Message thread only (file I/O); every change is load -> modify -> save, so two plugins open at once never undo each other.
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <map>

namespace ghosty::userbanks {

constexpr int kBase = 2000, kMax = 30000;   // s = 2000 + uid, uid 0 .. 29999 (a cell's sound number is 16-bit)
static const char* const kTypes[] = { "kick", "snare", "clap", "hat", "ohat", "perc", "bass", "fx", "crash" };
constexpr int kNumTypes = 9;

inline juce::File root() { return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("GhostBlock Banks"); }
inline juce::File& rootOverride() { static juce::File f; return f; }   // tests
inline juce::File dir() { return rootOverride() != juce::File() ? rootOverride() : root(); }
inline juce::File libFile() { return dir().getChildFile("library.json"); }
inline juce::int64 stamp() { const auto f = libFile(); return f.getLastModificationTime().toMilliseconds() * 1000003 + f.getSize(); }   // (mtime is only to the second on some disks: the size too)

inline bool isAudio(const juce::String& name) {
    const auto e = name.fromLastOccurrenceOf(".", false, false).toLowerCase();
    return e == "wav" || e == "aif" || e == "aiff" || e == "flac" || e == "mp3" || e == "ogg";
}
inline bool isType(const juce::String& t) { for (auto* k : kTypes) if (t == k) return true; return false; }

inline juce::var fresh() {
    auto* o = new juce::DynamicObject(); o->setProperty("v", 1); o->setProperty("nb", 4); o->setProperty("nu", 0);
    o->setProperty("banks", juce::Array<juce::var>()); o->setProperty("snd", juce::var(new juce::DynamicObject())); return juce::var(o);
}
inline juce::var load() {
    auto v = juce::JSON::parse(libFile().loadFileAsString());
    if (! v.isObject() || ! v["banks"].isArray() || ! v["snd"].isObject()) return fresh();
    return v;
}
inline bool save(const juce::var& lib) {
    if (! dir().createDirectory()) return false;
    const auto tmp = dir().getChildFile("library.json.tmp");
    if (! tmp.replaceWithText(juce::JSON::toString(lib, false))) return false;
    return tmp.moveFileTo(libFile());
}
inline juce::DynamicObject* sounds(juce::var& lib) { return lib["snd"].getDynamicObject(); }
inline juce::var* bankById(juce::var& lib, const juce::String& id) {
    if (auto* a = lib["banks"].getArray()) for (auto& b : *a) if (b["id"].toString() == id) return &b;
    return nullptr;
}
inline juce::String cleanName(juce::String s, int max = 40) {
    s = s.removeCharacters("\\/:*?\"<>|\r\n\t").trim(); if (s.isEmpty()) s = "SOUND"; return s.substring(0, max);
}
// a new empty bank; name "" -> MY BANK 4, MY BANK 5 ... (each plugin's own banks are 1 2 3)
inline juce::String newBank(juce::var& lib, const juce::String& name) {
    const int nb = juce::jmax(4, (int) lib["nb"]); lib.getDynamicObject()->setProperty("nb", nb + 1);
    juce::String id = "my" + juce::String(nb); int k = nb;
    while (bankById(lib, id) != nullptr) id = "my" + juce::String(++k);
    auto* o = new juce::DynamicObject(); o->setProperty("id", id); o->setProperty("name", cleanName(name.isNotEmpty() ? name : "MY BANK " + juce::String(nb), 48).toUpperCase());
    o->setProperty("s", juce::Array<juce::var>());
    lib["banks"].getArray()->add(juce::var(o)); return id;
}
inline juce::String bankNamed(juce::var& lib, const juce::String& name) {   // an existing bank with this name (any case), or ""
    const auto want = cleanName(name, 48).toUpperCase(); if (auto* a = lib["banks"].getArray()) for (auto& b : *a) if (b["name"].toString() == want) return b["id"].toString();
    return {};
}
inline juce::String findOrNewBank(juce::var& lib, const juce::String& name, bool& made) { auto id = bankNamed(lib, name); made = id.isEmpty(); return made ? newBank(lib, name) : id; }
inline bool renameBank(juce::var& lib, const juce::String& id, const juce::String& name) {
    if (auto* b = bankById(lib, id)) { b->getDynamicObject()->setProperty("name", cleanName(name, 48).toUpperCase()); return true; } return false;
}
inline void deleteSoundFile(juce::var& lib, int uid) {
    if (auto* S = sounds(lib)) { const auto f = S->getProperty(juce::String(uid))["f"].toString(); if (f.isNotEmpty()) dir().getChildFile(f).deleteFile(); S->removeProperty(juce::String(uid)); }
}
inline bool deleteBank(juce::var& lib, const juce::String& id) {
    auto* a = lib["banks"].getArray(); if (a == nullptr) return false;
    for (int i = 0; i < a->size(); ++i) if (a->getReference(i)["id"].toString() == id) {
        if (auto* s = a->getReference(i)["s"].getArray()) for (auto& u : *s) deleteSoundFile(lib, (int) u);
        a->remove(i); return true; }
    return false;
}
inline bool deleteSound(juce::var& lib, int uid) {
    if (auto* a = lib["banks"].getArray()) for (auto& b : *a) if (auto* s = b["s"].getArray()) s->removeAllInstancesOf(juce::var(uid));
    deleteSoundFile(lib, uid); return true;
}
inline bool setType(juce::var& lib, int uid, const juce::String& t) {
    if (! isType(t)) return false; auto* S = sounds(lib); if (S == nullptr || ! S->hasProperty(juce::String(uid))) return false;
    if (auto* o = S->getProperty(juce::String(uid)).getDynamicObject()) { o->setProperty("c", t); return true; } return false;
}

// ---------------------------------------------------------------------------------------------- what kind of drum is it?
// 1. the file name, 2. the folder names, 3. listening (length, brightness, low end). Every guess can be changed in the page.
inline juce::StringArray nameWords(const juce::String& in);
inline bool isLoopName(const juce::String& n) { const auto w = nameWords(n); for (auto& x : w) if (x.contains("loop")) return true;   // loop, loops, drumloop, toploop...
    const bool oneShot = w.contains("oneshot") || w.contains("shot") || w.contains("hit");   // "Hat One Shot Break" is a hit with a name
    return w.contains("lp") || w.contains("bpm") || (! oneShot && (w.contains("groove") || w.contains("break") || w.contains("breaks"))); }
// the words in a name: "KSHMR_Kick_01" -> kshmr kick 01, "OpenHat" -> open hat, "Snare808" -> snare 808 (whole words only: "Phat Kick" is a kick)
inline juce::StringArray nameWords(const juce::String& in) {
    juce::String t; juce::juce_wchar prev = 0;
    for (auto p = in.getCharPointer(); ! p.isEmpty(); ++p) {
        const auto c = *p; const bool alnum = juce::CharacterFunctions::isLetterOrDigit(c);
        if (! alnum) { t << ' '; prev = 0; continue; }
        if (prev != 0 && ((juce::CharacterFunctions::isLowerCase(prev) && juce::CharacterFunctions::isUpperCase(c))
                          || (juce::CharacterFunctions::isDigit(prev) != juce::CharacterFunctions::isDigit(c)))) t << ' ';
        t << juce::CharacterFunctions::toLowerCase(c); prev = c;
    }
    juce::StringArray w; w.addTokens(t, " ", ""); w.removeEmptyStrings(); return w;
}
inline juce::String typeFromName(const juce::String& name) {
    const auto w = nameWords(name);
    auto has = [&](std::initializer_list<const char*> keys) { for (auto* k : keys) if (w.contains(k)) return true; return false; };
    auto pair = [&](const char* a, std::initializer_list<const char*> bs) { const int i = w.indexOf(a); if (i < 0) return false; for (auto* b : bs) { const int j = w.indexOf(b); if (j >= 0 && std::abs(j - i) <= 2) return true; } return false; };
    if (has({ "vox", "vocal", "vocals", "adlib", "adlibs", "acapella", "acappella", "choir", "chant", "speech", "spoken" })) return "fx";   // first: sung words ("oh", "hey") are not drums
    const bool hatWord = has({ "hat", "hats", "hh", "hihat", "hihats", "hhat" }) || pair("hi", { "hat", "hats" });
    if (has({ "oh", "ohh", "ohat", "openhat", "openhh" }) || (hatWord && has({ "open", "opn" }))) return "ohat";
    if (hatWord || has({ "chh", "ch", "closedhat", "closedhh" })) return "hat";
    const bool snareWord = has({ "snare", "snares", "snr", "sd", "sn", "rimshot", "snre" });
    if (has({ "clap", "claps", "clp", "cp", "snap", "snaps", "clapz" }) && ! snareWord) return "clap";   // ("Snare & Clap" layers play as snares)
    if (snareWord) return "snare";
    if (has({ "kick", "kicks", "kik", "kck", "bd", "kd", "bassdrum", "kickdrum" }) || pair("bass", { "drum" })) return "kick";
    if (has({ "crash", "crashes", "cymbal", "cymbals", "cym", "ride", "rides", "splash", "china" })) return "crash";
    if (has({ "perc", "percs", "percussion", "tom", "toms", "conga", "congas", "bongo", "bongos", "tamb", "tambourine", "cowbell", "bell", "bells", "rim", "rims",
              "click", "wood", "woodblock", "block", "clave", "claves", "tabla", "guiro", "triangle", "agogo", "timbale", "timbales", "maraca", "maracas",
              "shaker", "shakers", "shake", "cabasa", "rattle", "foley", "knock", "djembe", "cajon", "taiko" })) return "perc";
    if (has({ "fx", "sfx", "riser", "risers", "sweep", "impact", "impacts", "noise", "vox", "vocal", "vocals", "chop", "chops", "uplifter", "downlifter",
              "reverse", "rev", "laser", "zap", "fill", "fills", "chord", "chords", "stab", "stabs", "synth", "synths", "pad", "pads", "lead", "leads", "atmos", "atmosphere", "texture", "drone",
              "keys", "key", "piano", "tone", "tones", "pluck", "plucks", "guitar", "strings", "brass", "flute", "organ", "rhodes", "melody", "melodic", "bells",
              "transition", "whoosh", "siren", "lazer", "downsweep", "upsweep", "glitch", "glitches", "scratch" }) && (has({ "fx", "sfx" }) || ! has({ "bass", "sub", "808", "reese", "bassline" }))) return "fx";   // ("Synth Bass" is a bass)
    if (has({ "808", "808s", "sub", "subs", "bass", "basses", "reese", "bassline" })) return "bass";   // last: "808" names a bass only when nothing else does
    return {};
}
inline juce::String typeFromSound(juce::AudioFormatReader& r) {
    const int n = (int) std::min<juce::int64>(r.lengthInSamples, (juce::int64) (r.sampleRate * 2.0));   // (std::min: juce::jmin<int64> trips juce_dsp SIMD overloads on GCC) if (n < 32) return "perc";
    juce::AudioBuffer<float> b(1, n); r.read(&b, 0, n, 0, true, false);
    const float* x = b.getReadPointer(0); const double sr = r.sampleRate;
    double e = 0, lo = 0, zc = 0, peak = 0; float lp = 0; const float a = (float) (1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * 150.0 / sr));
    int last = 0;
    for (int i = 0; i < n; ++i) { lp += a * (x[i] - lp); e += x[i] * x[i]; lo += lp * lp; if (i > 0 && (x[i] >= 0) != (x[i - 1] >= 0)) ++zc; peak = juce::jmax(peak, (double) std::abs(x[i])); }
    for (int i = n - 1; i >= 0; --i) if (std::abs(x[i]) > peak * 0.05) { last = i; break; }   // how long it rings (to -26 dB)
    const double len = (double) last / sr, low = e > 0 ? lo / e : 0, zcr = zc / (double) n * 44100.0 / sr;   // crossings per sample at 44.1k
    if (low > 0.55 && zcr < 0.03) return len > 0.9 ? "bass" : "kick";
    if (zcr > 0.22) return len < 0.22 ? "hat" : len < 1.3 ? "ohat" : "crash";
    if (len > 2.2) return "fx";
    if (zcr > 0.07 && low < 0.3) return len < 0.12 ? "clap" : "snare";
    return "perc";
}
// what kind of drum a FOLDER holds ("Kicks", "Hi-Hats", "808s", "Drum Loops"), ignoring kit words ("808 Kit" says nothing)
inline juce::String folderType(const juce::String& folder) {
    juce::StringArray w = nameWords(folder);
    for (int i = w.size(); --i >= 0;) if (w[i] == "808" || w[i] == "909" || w[i] == "707" || w[i] == "kit" || w[i] == "kits" || w[i] == "drums" || w[i] == "drum" || w[i] == "pack" || w[i] == "vol") w.remove(i);
    return typeFromName(w.joinIntoString(" "));
}
inline juce::String classify(const juce::File& f, const juce::String& folders, juce::AudioFormatManager& fm) {
    auto t = typeFromName(f.getFileNameWithoutExtension()); if (t.isNotEmpty()) return t;
    {   // a folder name only counts when it names a kind of drum ("Kicks", "Hats", "808s"), not a kit ("808 Kit", "Perc Pack" still do)
        juce::StringArray w; w.addTokens(folders.toLowerCase(), " _-", ""); w.removeEmptyStrings();
        for (int i = w.size(); --i >= 0;) { if (w[i] == "808" || w[i] == "909" || w[i] == "707" || w[i] == "kit" || w[i] == "kits" || w[i] == "drums" || w[i] == "pack") w.remove(i); }
        t = typeFromName(w.joinIntoString(" ")); if (t.isNotEmpty()) return t; }
    if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor(f) }) return typeFromSound(*r);
    return "perc";
}

inline juce::String prettyName(juce::String n, const juce::String& strip) {
    if (strip.isNotEmpty() && n.startsWithIgnoreCase(strip) && n.length() - strip.length() >= 3) n = n.substring(strip.length());
    n = n.replaceCharacter('_', ' ').trim(); while (n.contains("  ")) n = n.replace("  ", " ");
    return n;
}
// the start every file in a folder shares, up to a separator ("MB_AKW_100_")
inline juce::String sharedPrefix(const juce::Array<juce::File>& fs) {
    if (fs.size() < 3) return {}; juce::String p = fs[0].getFileNameWithoutExtension();
    for (auto& f : fs) { const auto n = f.getFileNameWithoutExtension(); int k = 0; while (k < p.length() && k < n.length() && juce::CharacterFunctions::toLowerCase(p[k]) == juce::CharacterFunctions::toLowerCase(n[k])) ++k; p = p.substring(0, k); }
    const int cut = juce::jmax(p.lastIndexOfChar('_'), p.lastIndexOfChar(' '), p.lastIndexOfChar('-')); return cut >= 2 ? p.substring(0, cut + 1) : juce::String();
}
// ---------------------------------------------------------------------------------------------- adding sounds
struct Added { int sounds = 0, banks = 0, skipped = 0; juce::String lastBank; juce::String why; };

// copy one audio file into the library and put it in bank 'id'. folders: the folder names it came from (type hints)
inline bool addFile(juce::var& lib, const juce::String& id, const juce::File& src, const juce::String& folders, juce::AudioFormatManager& fm, Added& out, const juce::String& strip = {}, const juce::String& forced = {}) {
    auto* b = bankById(lib, id); if (b == nullptr || ! isAudio(src.getFileName()) || ! src.existsAsFile()) { ++out.skipped; return false; }
    if (src.getSize() > 40 * 1024 * 1024) { ++out.skipped; out.why = "a file was over 40 MB"; return false; }
    double secs = 0;
    { std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor(src) }; if (r == nullptr || r->lengthInSamples <= 0) { ++out.skipped; out.why = "a file couldn't be read"; return false; } secs = (double) r->lengthInSamples / r->sampleRate; }
    // a tempo in the name ("TSP_KOAN_126_top_clatter") on anything longer than a hit = a loop
    bool tempo = false; for (auto& x : nameWords(src.getFileNameWithoutExtension())) if (x.containsOnly("0123456789") && x.getIntValue() >= 60 && x.getIntValue() <= 200 && x.length() >= 2) tempo = true;
    const bool loop = isLoopName(src.getFileName()) || isLoopName(src.getParentDirectory().getFileName()) || (tempo && secs > 1.5) || (secs > 2.5 && nameWords(src.getFileNameWithoutExtension()).contains("top"));
    const juce::String nm = cleanName(prettyName(src.getFileNameWithoutExtension(), strip), 80);
    if (auto* arr = (*b)["s"].getArray()) for (auto& u : *arr) if (lib["snd"][juce::Identifier(juce::String((int) u))]["n"].toString() == nm) { ++out.skipped; out.why = "some were already in that bank"; return false; }   // dropped twice: once is enough
    const int uid = juce::jmax(0, (int) lib["nu"]); if (uid >= kMax) { ++out.skipped; out.why = "the library is full (30000 sounds)"; return false; }
    const auto sd = dir().getChildFile("sounds"); if (! sd.createDirectory()) { ++out.skipped; out.why = "couldn't write to Music/GhostBlock Banks"; return false; }
    const auto dst = sd.getChildFile(juce::String(uid) + "-" + cleanName(nm, 60) + "." + src.getFileExtension().trimCharactersAtStart(".").toLowerCase());
    if (! src.copyFileTo(dst)) { ++out.skipped; out.why = "couldn't copy a file"; return false; }
    lib.getDynamicObject()->setProperty("nu", uid + 1);
    auto* o = new juce::DynamicObject(); o->setProperty("n", nm); o->setProperty("f", "sounds/" + dst.getFileName());
    if (loop) { o->setProperty("c", "fx"); o->setProperty("lp", true); }   // ("Drum Loops/Bounce 3.wav" too)   // a LOOP: kept in the bank, never used as a single drum hit by GENERATE / the arrows
    else o->setProperty("c", isType(forced) ? forced : classify(src, folders + " " + src.getParentDirectory().getFileName(), fm));   // (the original's name: every word in it counts)
    sounds(lib)->setProperty(juce::String(uid), juce::var(o));
    if (auto* arr = (*b)["s"].getArray()) arr->add(uid);
    ++out.sounds; out.lastBank = id; return true;
}
// a sound from bytes (a built-in sound copied out of a plugin): its type is known, so no guessing
inline bool addBytes(juce::var& lib, const juce::String& id, const juce::String& name, const juce::String& ext, const juce::MemoryBlock& data, const juce::String& type, Added& out) {
    if (bankById(lib, id) == nullptr || data.getSize() == 0) { ++out.skipped; return false; }
    const int uid = juce::jmax(0, (int) lib["nu"]); if (uid >= kMax) { ++out.skipped; out.why = "the library is full (30000 sounds)"; return false; }
    const auto sd = dir().getChildFile("sounds"); if (! sd.createDirectory()) { ++out.skipped; out.why = "couldn't write to Music/GhostBlock Banks"; return false; }
    const juce::String nm = cleanName(name), e = ext.trimCharactersAtStart(".").toLowerCase();
    const auto dst = sd.getChildFile(juce::String(uid) + "-" + cleanName(nm, 60) + "." + (isAudio("x." + e) ? e : juce::String("wav")));
    if (! dst.replaceWithData(data.getData(), data.getSize())) { ++out.skipped; out.why = "couldn't write a file"; return false; }
    lib.getDynamicObject()->setProperty("nu", uid + 1);
    auto* o = new juce::DynamicObject(); o->setProperty("n", nm); o->setProperty("c", isType(type) ? type : juce::String("perc")); o->setProperty("f", "sounds/" + dst.getFileName());
    sounds(lib)->setProperty(juce::String(uid), juce::var(o));
    if (auto* arr = (*bankById(lib, id))["s"].getArray()) arr->add(uid);
    ++out.sounds; out.lastBank = id; return true;
}
// one of your sounds into another bank (a real copy: deleting one never touches the other)
inline bool copySound(juce::var& lib, const juce::String& id, int uid, Added& out) {
    const auto so = lib["snd"][juce::Identifier(juce::String(uid))]; if (! so.isObject()) { ++out.skipped; return false; }
    juce::MemoryBlock mb; const auto f = dir().getChildFile(so["f"].toString()); if (! f.loadFileAsData(mb)) { ++out.skipped; out.why = "a sound file is missing"; return false; }
    return addBytes(lib, id, so["n"].toString(), f.getFileExtension(), mb, so["c"].toString(), out);
}
// files and folders from the chooser: loose files go into 'into' (or a new bank); every folder that holds sounds becomes its own bank,
// named after the folder (a folder of folders: each inner folder is a bank)
inline Added addPaths(juce::var& lib, const juce::Array<juce::File>& items, juce::String into, juce::AudioFormatManager& fm) {
    Added out; juce::Array<juce::File> loose;
    std::function<void(const juce::File&, const juce::String&)> walk = [&](const juce::File& d, const juce::String& trail) {
        auto subs0 = d.findChildFiles(juce::File::findDirectories, false); subs0.sort();
        {   // an already-sorted pack (Kicks / Snares / Hi-Hats / 808s / Drum Loops ... folders): ONE bank named after the pack, the folders say the type
            int typed = 0, n = 0; for (auto& sd : subs0) { if (sd.getFileName().startsWithChar('.')) continue; ++n; if (folderType(sd.getFileName()).isNotEmpty() || isLoopName(sd.getFileName())) ++typed; }
            if (n >= 2 && typed * 2 > n) {
                auto all = d.findChildFiles(juce::File::findFiles, true); all.sort(); juce::Array<juce::File> snd; for (auto& f : all) if (isAudio(f.getFileName()) && ! f.getFileName().startsWithChar('.')) snd.add(f);
                if (! snd.isEmpty()) { bool made = false; const auto id = findOrNewBank(lib, d.getFileName(), made); if (made) ++out.banks; out.lastBank = id;
                    for (auto& f : snd) { const auto fd = f.getParentDirectory().getFileName(); addFile(lib, id, f, fd, fm, out, {}, f.getParentDirectory() == d ? juce::String() : folderType(fd)); } }
                return;
            }
        }
        auto files = d.findChildFiles(juce::File::findFiles, false); files.sort();
        juce::Array<juce::File> snd; for (auto& f : files) if (isAudio(f.getFileName())) snd.add(f);
        if (! snd.isEmpty()) { bool made = false; const auto id = findOrNewBank(lib, d.getFileName(), made); if (made) ++out.banks; out.lastBank = id;
            for (auto& f : snd) addFile(lib, id, f, trail + " " + d.getFileName(), fm, out); }
        auto subs = d.findChildFiles(juce::File::findDirectories, false); subs.sort();
        for (auto& s : subs) if (! s.getFileName().startsWithChar('.')) walk(s, trail + " " + d.getFileName());
    };
    for (auto& f : items) { if (f.isDirectory()) walk(f, {}); else if (isAudio(f.getFileName())) loose.add(f); }
    if (! loose.isEmpty()) {
        if (into.isEmpty() || bankById(lib, into) == nullptr) { into = newBank(lib, {}); ++out.banks; }
        for (auto& f : loose) addFile(lib, into, f, {}, fm, out);
    }
    return out;
}

} // namespace ghosty::userbanks
