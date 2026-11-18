// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Import.h"
#include "Dsp.h"
#include <regex>
#include <set>

namespace atlas::importer
{
juce::AudioFormatManager& audioFormats()
{
    static juce::AudioFormatManager* fm = [] { auto* f = new juce::AudioFormatManager(); f->registerBasicFormats(); return f; }();
    return *fm;
}
bool isAudioFile (const juce::File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return e == ".wav" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".ogg" || e == ".mp3";
}
static bool isHidden (const juce::File& f) { return f.getFileName().startsWith (".") || f.getFileName() == "__MACOSX"; }

static void collect (const juce::File& dir, juce::Array<juce::File>& out, int depth = 0)
{
    if (depth > 12) return;
    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto f = e.getFile();
        if (isHidden (f)) continue;
        if (f.isDirectory()) { if (f.getFileName() != "_atlas") collect (f, out, depth + 1); }
        else out.add (f);
    }
}

juce::String folderSignature (const juce::File& folder)
{
    juce::Array<juce::File> files; collect (folder, files);
    juce::StringArray lines;
    for (const auto& f : files)
        if (isAudioFile (f) || f.hasFileExtension ("sfz;sf2;dspreset"))
            lines.add (f.getRelativePathFrom (folder).replaceCharacter ('\\', '/').toLowerCase() + ":" + juce::String (f.getSize()));
    if (lines.isEmpty()) return {};
    lines.sort (false);
    // (a fingerprint to spot the same library dropped twice, not a security hash)
    const auto text = lines.joinIntoString ("\n");
    return juce::String::toHexString (text.hashCode64()) + "-" + juce::String (lines.size()) + "-" + juce::String::toHexString ((juce::int64) text.length());
}

bool copyFolder (const juce::File& from, const juce::File& to, const Progress& progress)
{
    juce::Array<juce::File> files; collect (from, files);
    if (! to.createDirectory()) return false;
    int k = 0;
    for (const auto& f : files)
    {
        const auto dst = to.getChildFile (f.getRelativePathFrom (from));
        dst.getParentDirectory().createDirectory();
        if (! f.copyFileTo (dst)) return false;
        if (progress && (++k % 25) == 0 && ! progress ((float) k / (float) files.size(), f.getFileName())) return false;
    }
    return true;
}

void readSourceRecord (const juce::File& folder, LibrarySource& src)
{
    const auto rec = readJson (folder.getChildFile ("ATLAS-SOURCE.json"));
    if (rec.isObject())
    {
        src.name = prop (rec, "name", src.name).toString(); src.author = prop (rec, "author").toString(); src.credit = prop (rec, "credit").toString();
        src.license = prop (rec, "license").toString(); src.licenseUrl = prop (rec, "license_url").toString(); src.source = prop (rec, "source").toString();
        src.restrictions = prop (rec, "restrictions").toString(); src.version = prop (rec, "version").toString();
        src.category = prop (rec, "category").toString();
        src.prefix = prop (rec, "prefix").toString();
        src.skip.clear(); src.keep.clear(); src.singles.clear(); src.rename.clear(); src.categories.clear();
        if (const auto* a = prop (rec, "singles").getArray()) for (const auto& v : *a) src.singles.add (v.toString());
        if (const auto* a = prop (rec, "keep").getArray()) for (const auto& v : *a) src.keep.add (v.toString());
        if (const auto* a = prop (rec, "skip").getArray()) for (const auto& v : *a) src.skip.add (v.toString());
        if (const auto* o = prop (rec, "rename").getDynamicObject()) for (const auto& kv : o->getProperties()) src.rename.emplace_back (kv.name.toString(), kv.value.toString());
        src.waves.clear();
        if (const auto* a = prop (rec, "waves").getArray())
            for (const auto& v : *a)
            {
                LibrarySource::Wave w;
                w.name = prop (v, "name").toString(); w.file = prop (v, "file").toString(); w.shape = prop (v, "shape", "hold").toString(); w.category = prop (v, "category").toString();
                w.multi = juce::jlimit (1, 9, (int) prop (v, "multi", 1)); w.detune = juce::jlimit (0.0f, 100.0f, (float) (double) prop (v, "detune", 0.0));
                if (w.name.isNotEmpty() && w.file.isNotEmpty()) src.waves.push_back (w);
            }
        if (const auto* a = prop (rec, "categories").getArray())
            for (const auto& v : *a) if (const auto* pair = v.getArray()) if (pair->size() == 2) src.categories.emplace_back ((*pair)[0].toString(), (*pair)[1].toString());
        return;
    }
    // no record: look for the library's own licence or readme and say what was found (never guess a licence)
    juce::Array<juce::File> files; collect (folder, files);
    for (const auto& f : files)
    {
        const auto n = f.getFileName().toLowerCase();
        if (n.startsWith ("license") || n.startsWith ("licence") || n.startsWith ("copying") || n.contains ("readme") || n.contains ("eula"))
        {
            const auto text = f.loadFileAsString().substring (0, 4000);
            juce::String lic;
            if (text.containsIgnoreCase ("CC0") || text.containsIgnoreCase ("public domain dedication")) lic = "CC0-1.0";
            else if (text.containsIgnoreCase ("Attribution-NonCommercial")) lic = "CC-BY-NC";
            else if (text.containsIgnoreCase ("Attribution 4.0")) lic = "CC-BY-4.0";
            else if (text.containsIgnoreCase ("Attribution 3.0")) lic = "CC-BY-3.0";
            else if (text.containsIgnoreCase ("Sampling Plus")) lic = "CC-Sampling-Plus-1.0";
            else if (text.containsIgnoreCase ("GNU GENERAL PUBLIC LICENSE")) lic = "GPL";
            else if (text.containsIgnoreCase ("pianobook")) lic = "Pianobook terms";
            if (lic.isNotEmpty()) { src.license = lic + " (read from " + f.getFileName() + ")"; return; }
            if (src.license.isEmpty()) src.license = "see " + f.getFileName();
        }
    }
}

// ============================================================================================ names
// a file name as a person would write it: "SynthStrings1 20200528" -> "Synth Strings 1", "cowsynth_fancy_pad" -> "Cowsynth Fancy Pad"
static juce::String pretty (juce::String s)
{
    s = s.replaceCharacters ("_", " ");
    // date stamps (20190924, 2019-09-24) say nothing about the sound
    {
        juce::String t;
        for (int i = 0; i < s.length();)
        {
            int j = i; while (j < s.length() && juce::CharacterFunctions::isDigit (s[j])) ++j;
            const bool stamp = j - i == 8 && (s.substring (i, i + 2) == "19" || s.substring (i, i + 2) == "20");
            if (stamp) { while (t.isNotEmpty() && (t.getLastCharacter() == '-' || t.getLastCharacter() == ' ')) t = t.dropLastCharacters (1); i = j; continue; }
            if (j > i) { t << s.substring (i, j); i = j; } else t << s[i++];
        }
        s = t;
    }
    juce::String out;
    for (int i = 0; i < s.length(); ++i)
    {
        const auto c = s[i];
        if (i > 0)
        {
            const auto p = s[i - 1];
            const bool lowerUpper = juce::CharacterFunctions::isLowerCase (p) && juce::CharacterFunctions::isUpperCase (c);
            const bool wordDigit = juce::CharacterFunctions::isLowerCase (p) && juce::CharacterFunctions::isDigit (c);
            if (lowerUpper || wordDigit) out << ' ';
        }
        out << c;
    }
    while (out.contains ("  ")) out = out.replace ("  ", " ");
    out = out.trim();
    // a name written all in small letters gets capitals; anything else is left as its author wrote it
    juce::StringArray words; words.addTokens (out, " ", "");
    for (auto& w : words)
        if (w.isNotEmpty() && w == w.toLowerCase() && juce::CharacterFunctions::isLetter (w[0])) w = w.substring (0, 1).toUpperCase() + w.substring (1);
    return words.joinIntoString (" ");
}

static int noteFromToken (const juce::String& tok)
{
    const auto t = tok.toLowerCase();
    const int len = t.length();
    if (len < 2 || len > 4) return -1;
    const auto l = t[0];
    if (l < 'a' || l > 'g') return -1;
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };
    int semis = base[l - 'a'], i = 1;
    if (t[i] == '#') { ++semis; ++i; }
    else if ((t[i] == 'b' || t[i] == 's') && i + 1 < len && (juce::CharacterFunctions::isDigit (t[i + 1]) || t[i + 1] == '-')) { semis += (t[i] == 'b' ? -1 : 1); ++i; }
    if (i >= len) return -1;
    bool neg = false;
    if (t[i] == '-') { neg = true; ++i; }
    if (i != len - 1 || ! juce::CharacterFunctions::isDigit (t[i])) return -1;
    const int oct = (neg ? -1 : 1) * (int) (t[i] - '0');
    const int note = (oct + 1) * 12 + semis;
    return (note >= 0 && note <= 127) ? note : -1;
}

static int dynamicRank (const juce::String& t)
{
    static const std::map<juce::String, int> d = { { "ppp", 10 }, { "pp", 20 }, { "p", 30 }, { "mp", 40 }, { "mf", 50 }, { "f", 60 }, { "ff", 70 }, { "fff", 80 },
                                                   { "soft", 25 }, { "med", 50 }, { "medium", 50 }, { "hard", 70 }, { "loud", 70 }, { "quiet", 25 } };
    const auto it = d.find (t.toLowerCase());
    return it == d.end() ? -1 : it->second;
}

LooseSample parseSampleName (const juce::File& file)
{
    LooseSample s;
    s.file = file;
    const auto stem = file.getFileNameWithoutExtension();
    juce::StringArray toks;
    toks.addTokens (stem, "_- .()[],", "");
    toks.removeEmptyStrings();
    juce::StringArray rest;
    for (const auto& tk : toks)
    {
        const auto t = tk.toLowerCase();
        const int n = noteFromToken (tk);
        if (n >= 0 && s.note < 0) { s.note = n; s.noteFromName = true; continue; }
        if (t.length() >= 2 && (t.startsWith ("vel") || t.startsWith ("vl") || t[0] == 'v') && t.substring (t.startsWith ("vel") ? 3 : (t.startsWith ("vl") ? 2 : 1)).containsOnly ("0123456789")
            && t.substring (t.startsWith ("vel") ? 3 : (t.startsWith ("vl") ? 2 : 1)).isNotEmpty())
        { s.velocityRank = 100 + t.substring (t.startsWith ("vel") ? 3 : (t.startsWith ("vl") ? 2 : 1)).getIntValue(); continue; }
        if (dynamicRank (t) >= 0 && s.velocityRank < 0) { s.velocityRank = dynamicRank (t); continue; }
        if ((t.startsWith ("rr") && t.substring (2).containsOnly ("0123456789") && t.length() > 2) || (t.startsWith ("round") && t.substring (5).containsOnly ("0123456789") && t.length() > 5))
        { s.roundRobin = t.retainCharacters ("0123456789").getIntValue(); continue; }
        if ((t.startsWith ("note") || t[0] == 'n' || t[0] == 'm') && t.substring (t.startsWith ("note") ? 4 : 1).containsOnly ("0123456789") && t.substring (t.startsWith ("note") ? 4 : 1).length() >= 2 && s.note < 0)
        { const int v = t.substring (t.startsWith ("note") ? 4 : 1).getIntValue(); if (v >= 0 && v <= 127) { s.note = v; s.noteFromName = true; continue; } }
        if (t.containsOnly ("0123456789")) continue;                       // a bare number: an index, not part of the articulation
        rest.add (t.trimCharactersAtEnd ("0123456789"));                   // glass1 / glass2 are the same instrument
    }
    if (s.note < 0)
    {
        // a note written into a word: "PianoC4", "bellF#3"
        static const std::regex re ("([A-G])([#b]?)(-?[0-9])(?![0-9])");
        const std::string st = stem.toStdString();
        std::smatch m; std::string::const_iterator b = st.begin(); int last = -1;
        while (std::regex_search (b, st.cend(), m, re)) { last = noteFromToken (juce::String (m.str (1) + m.str (2) + m.str (3))); b = m.suffix().first; }
        if (last >= 0) { s.note = last; s.noteFromName = true; }
    }
    rest.removeEmptyStrings();
    s.group = rest.joinIntoString (" ");
    return s;
}

static void readAudioFacts (LooseSample& s)
{
    std::unique_ptr<juce::AudioFormatReader> r (audioFormats().createReaderFor (s.file));
    if (r == nullptr) return;
    s.sampleRate = r->sampleRate; s.frames = r->lengthInSamples; s.channels = (int) r->numChannels;
    const auto& md = r->metadataValues;
    if (s.note < 0 && md.containsKey ("MidiUnityNote")) { const int n = md["MidiUnityNote"].getIntValue(); if (n > 0 && n < 128 && md.getValue ("NumSampleLoops", "0").getIntValue() >= 0 && md.containsKey ("NumSampleLoops")) s.note = n; }
    if (md.getValue ("NumSampleLoops", "0").getIntValue() > 0)
    {
        const auto a = md.getValue ("Loop0Start", "-1").getLargeIntValue(), b = md.getValue ("Loop0End", "-1").getLargeIntValue();
        if (a >= 0 && b > a + 64 && b <= s.frames) { s.loopStart = a; s.loopEnd = b; }
    }
}

// the highest peak among some of a set of recordings (a dozen, spread across the set), as gain: 0 if none could be read
static float peakOfSome (const std::vector<LooseSample>& samples)
{
    float top = 0.0f;
    const size_t step = std::max<size_t> (1, samples.size() / 12);
    for (size_t i = 0; i < samples.size(); i += step)
    {
        std::unique_ptr<juce::AudioFormatReader> r (audioFormats().createReaderFor (samples[i].file));
        if (r == nullptr || r->lengthInSamples <= 0) continue;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 20.0));
        juce::AudioBuffer<float> b ((int) juce::jmax (1u, r->numChannels), n);
        r->read (&b, 0, n, 0, true, true);
        for (int c = 0; c < b.getNumChannels(); ++c) top = juce::jmax (top, b.getMagnitude (c, 0, n));
    }
    return top;
}

// what pitch a recording really is (MIDI note, fractional), or -1
static float detectNote (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> r (audioFormats().createReaderFor (file));
    if (r == nullptr || r->lengthInSamples < 4096) return -1.0f;
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 0.6));
    const juce::int64 start = std::min<juce::int64> ((juce::int64) ((double) r->lengthInSamples * 0.15), r->lengthInSamples - n);
    juce::AudioBuffer<float> buf ((int) r->numChannels, n);
    r->read (&buf, 0, n, start, true, true);
    const auto mono = measure::monoOf (buf);
    float clarity = 0;
    const float hz = measure::pitchOf (mono.data(), (int) mono.size(), r->sampleRate, clarity);
    if (hz <= 0 || clarity < 0.6f) return -1.0f;
    return 69.0f + 12.0f * std::log2 (hz / 440.0f);
}

juce::String mapToSfz (std::vector<LooseSample> samples, const juce::String& defaultPath, const juce::String& title, juce::StringArray& notes)
{
    samples.erase (std::remove_if (samples.begin(), samples.end(), [] (const LooseSample& s) { return s.note < 0 || s.frames <= 0; }), samples.end());
    if (samples.empty()) return {};

    // do the names count octaves from C4 = middle C (SFZ's way) or C3? Ask the recordings themselves.
    {
        std::vector<LooseSample*> named;
        for (auto& s : samples) if (s.noteFromName) named.push_back (&s);
        std::sort (named.begin(), named.end(), [] (const LooseSample* a, const LooseSample* b) { return a->frames > b->frames; });
        std::vector<int> shifts;
        for (size_t i = 0; i < named.size() && i < 6 && shifts.size() < 4; ++i)
        {
            const float heard = detectNote (named[i]->file);
            if (heard < 0) continue;
            const float diff = heard - (float) named[i]->note;
            const int oct = (int) std::lround (diff / 12.0f);
            if (std::abs (diff - 12.0f * (float) oct) < 1.5f) shifts.push_back (oct);
        }
        if (shifts.size() >= 2)
        {
            std::sort (shifts.begin(), shifts.end());
            const int med = shifts[shifts.size() / 2];
            const int agree = (int) std::count (shifts.begin(), shifts.end(), med);
            if (med != 0 && std::abs (med) <= 2 && agree * 2 > (int) shifts.size())
            {
                for (auto& s : samples) if (s.noteFromName) s.note = juce::jlimit (0, 127, s.note + 12 * med);
                notes.add ("the names count octaves " + juce::String (std::abs (med)) + " " + (med > 0 ? "lower" : "higher") + " than the recordings sound: mapped by what was heard");
            }
        }
    }

    // velocity layers: the distinct ranks, in order
    std::vector<int> ranks;
    for (const auto& s : samples) if (s.velocityRank >= 0 && std::find (ranks.begin(), ranks.end(), s.velocityRank) == ranks.end()) ranks.push_back (s.velocityRank);
    std::sort (ranks.begin(), ranks.end());
    const int layers = juce::jmax (1, (int) ranks.size());
    auto layerOf = [&ranks] (const LooseSample& s) { if (s.velocityRank < 0 || ranks.empty()) return 0; return (int) (std::find (ranks.begin(), ranks.end(), s.velocityRank) - ranks.begin()); };

    std::vector<int> keys;
    for (const auto& s : samples) if (std::find (keys.begin(), keys.end(), s.note) == keys.end()) keys.push_back (s.note);
    std::sort (keys.begin(), keys.end());
    auto rangeOf = [&keys] (int note, int& lo, int& hi) {
        const size_t i = (size_t) (std::find (keys.begin(), keys.end(), note) - keys.begin());
        lo = i == 0 ? juce::jmax (0, note - 12) : (keys[i - 1] + note) / 2 + 1;
        hi = i + 1 >= keys.size() ? juce::jmin (127, note + 12) : (note + keys[i + 1]) / 2;
    };

    bool anyLoop = false; double longest = 0;
    for (const auto& s : samples) { anyLoop = anyLoop || s.loopEnd > 0; if (s.sampleRate > 0) longest = juce::jmax (longest, (double) s.frames / s.sampleRate); }

    juce::String t;
    t << "// " << title << "\n// Mapped by Ghost Atlas from the file names. The recordings are not changed.\n";
    t << "<control> default_path=" << defaultPath << "\n";
    t << "<global> ampeg_release=" << (anyLoop || longest > 2.5 ? "0.45" : "0.30") << " amp_veltrack=" << (layers > 1 ? "60" : "85");
    {   // recordings made very quietly are played louder, so the instrument sits where others do (the files are not changed)
        const float top = peakOfSome (samples);
        if (top > 1.0e-5f && top < 0.25f)
        {
            const int up = juce::jlimit (0, 36, (int) std::floor (juce::Decibels::gainToDecibels (0.5f / top)));
            if (up >= 3) { t << " volume=" << up; notes.add ("its recordings are quiet: they are played " + juce::String (up) + " dB louder (the files are not changed)"); }
        }
    }
    t << "\n";
    for (int layer = 0; layer < layers; ++layer)
    {
        const int lovel = layer == 0 ? 1 : (int) std::lround (127.0 * layer / layers) + 1, hivel = (int) std::lround (127.0 * (layer + 1) / layers);
        for (int key : keys)
        {
            std::vector<const LooseSample*> here;
            for (const auto& s : samples) if (s.note == key && layerOf (s) == layer) here.push_back (&s);
            if (here.empty()) continue;
            std::sort (here.begin(), here.end(), [] (const LooseSample* a, const LooseSample* b) { return a->roundRobin != b->roundRobin ? a->roundRobin < b->roundRobin : a->file.getFileName() < b->file.getFileName(); });
            int lo, hi; rangeOf (key, lo, hi);
            t << "<group> lokey=" << lo << " hikey=" << hi << " pitch_keycenter=" << key << " lovel=" << lovel << " hivel=" << hivel;
            if (here.size() > 1) t << " seq_length=" << (int) here.size();
            t << "\n";
            int pos = 1;
            for (const auto* s : here)
            {
                t << "<region> sample=" << s->file.getFileName();
                if (here.size() > 1) t << " seq_position=" << pos++;
                if (s->loopEnd > 0) t << " loop_mode=loop_continuous loop_start=" << s->loopStart << " loop_end=" << s->loopEnd;
                t << "\n";
            }
        }
    }
    if (layers > 1) notes.add (juce::String (layers) + " velocity layers found in the names");
    return t;
}

// ============================================================================================ Decent Sampler
static int dsNote (const juce::String& v, int fallback)
{
    if (v.isEmpty()) return fallback;
    if (v.containsOnly ("0123456789")) return juce::jlimit (0, 127, v.getIntValue());
    const int n = noteFromToken (v);
    return n >= 0 ? n : fallback;
}
static float dsDb (const juce::String& v)
{
    if (v.isEmpty()) return 0.0f;
    if (v.containsIgnoreCase ("db")) return v.upToFirstOccurrenceOf ("d", false, true).getFloatValue();
    const float lin = v.getFloatValue();
    return lin > 0 ? gainToDb (lin) : -60.0f;
}

std::vector<Found> convertDecentSampler (const juce::File& dspreset, const juce::File& outDir, juce::StringArray& log)
{
    std::vector<Found> out;
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (dspreset));
    if (xml == nullptr || ! xml->hasTagName ("DecentSampler")) { log.add ("  " + dspreset.getFileName() + ": not a Decent Sampler preset that can be read"); return out; }
    auto* groups = xml->getChildByName ("groups");
    if (groups == nullptr) { log.add ("  " + dspreset.getFileName() + ": it has no sample groups"); return out; }

    auto attr = [] (const juce::XmlElement* a, const juce::XmlElement* b, const juce::XmlElement* c, const char* name) {
        for (auto* e : { a, b, c }) if (e != nullptr && e->hasAttribute (name)) return e->getStringAttribute (name);
        return juce::String();
    };
    outDir.createDirectory();
    const auto sfz = outDir.getChildFile (safeName (dspreset.getFileNameWithoutExtension()) + ".sfz");
    juce::String t;
    t << "// " << dspreset.getFileName() << "\n// Converted by Ghost Atlas from a Decent Sampler preset. The recordings are not changed.\n";
    t << "<control> default_path=" << dspreset.getParentDirectory().getRelativePathFrom (outDir).replaceCharacter ('\\', '/') << "/\n";
    int regions = 0;
    for (auto* g : groups->getChildWithTagNameIterator ("group"))
    {
        if (g->getStringAttribute ("enabled", "true") == "false") continue;
        const auto seqMode = attr (g, groups, nullptr, "seqMode");
        int seqLen = 0;
        if (seqMode.containsIgnoreCase ("round_robin") || seqMode.containsIgnoreCase ("random"))
            for (auto* s : g->getChildWithTagNameIterator ("sample")) seqLen = juce::jmax (seqLen, attr (s, g, nullptr, "seqPosition").getIntValue());
        const bool isRelease = attr (g, groups, nullptr, "trigger") == "release";
        t << "<group>";
        const auto a = attr (g, groups, nullptr, "attack"), d = attr (g, groups, nullptr, "decay"), su = attr (g, groups, nullptr, "sustain"), r = attr (g, groups, nullptr, "release");
        if (a.isNotEmpty()) t << " ampeg_attack=" << a.getFloatValue();
        if (d.isNotEmpty()) t << " ampeg_decay=" << d.getFloatValue();
        if (su.isNotEmpty()) t << " ampeg_sustain=" << juce::jlimit (0.0f, 100.0f, su.getFloatValue() * 100.0f);
        if (r.isNotEmpty()) t << " ampeg_release=" << r.getFloatValue();
        const float gdb = dsDb (attr (g, groups, nullptr, "volume"));
        if (std::abs (gdb) > 0.01f) t << " volume=" << gdb;
        const auto gt = attr (g, groups, nullptr, "tuning"); if (gt.isNotEmpty()) t << " tune=" << (int) std::lround (gt.getFloatValue() * 100.0f);
        const auto gp = attr (g, groups, nullptr, "pan"); if (gp.isNotEmpty()) t << " pan=" << juce::jlimit (-100.0f, 100.0f, gp.getFloatValue());
        const auto vt = attr (g, groups, nullptr, "ampVelTrack"); if (vt.isNotEmpty()) t << " amp_veltrack=" << juce::jlimit (0.0f, 100.0f, vt.getFloatValue() * 100.0f);
        if (isRelease) t << " trigger=release";
        if (seqMode.containsIgnoreCase ("random") && seqLen > 1) { /* random is written per region below */ }
        t << "\n";
        for (auto* s : g->getChildWithTagNameIterator ("sample"))
        {
            auto path = s->getStringAttribute ("path").replaceCharacter ('\\', '/');
            if (path.isEmpty()) continue;
            const int root = dsNote (s->getStringAttribute ("rootNote"), 60);
            const int lo = dsNote (s->getStringAttribute ("loNote"), root), hi = dsNote (s->getStringAttribute ("hiNote"), root);
            t << "<region> sample=" << path << " pitch_keycenter=" << root << " lokey=" << juce::jmin (lo, hi) << " hikey=" << juce::jmax (lo, hi);
            if (s->hasAttribute ("loVel")) t << " lovel=" << juce::jlimit (0, 127, s->getIntAttribute ("loVel"));
            if (s->hasAttribute ("hiVel")) t << " hivel=" << juce::jlimit (0, 127, s->getIntAttribute ("hiVel"));
            if (s->hasAttribute ("start")) t << " offset=" << s->getIntAttribute ("start");
            if (s->hasAttribute ("end")) t << " end=" << s->getIntAttribute ("end");
            if (s->hasAttribute ("tuning")) t << " tune=" << (int) std::lround (s->getDoubleAttribute ("tuning") * 100.0);
            if (s->hasAttribute ("volume")) t << " volume=" << dsDb (s->getStringAttribute ("volume")) + 0.0f;
            if (s->hasAttribute ("pan")) t << " pan=" << juce::jlimit (-100.0, 100.0, s->getDoubleAttribute ("pan"));
            const auto le = attr (s, g, groups, "loopEnabled");
            if ((le == "true" || le == "1") && s->hasAttribute ("loopStart") && s->hasAttribute ("loopEnd"))
            {
                t << " loop_mode=loop_continuous loop_start=" << s->getIntAttribute ("loopStart") << " loop_end=" << s->getIntAttribute ("loopEnd");
                const auto xf = attr (s, g, groups, "loopCrossfade");
                if (xf.isNotEmpty() && xf.getIntValue() > 0) t << " loop_crossfade=" << juce::jlimit (0.001, 1.0, xf.getDoubleValue() / 44100.0);
            }
            if (seqLen > 1 && seqMode.containsIgnoreCase ("round_robin")) t << " seq_length=" << seqLen << " seq_position=" << juce::jmax (1, attr (s, g, nullptr, "seqPosition").getIntValue());
            if (seqLen > 1 && seqMode.containsIgnoreCase ("random"))
            { const int p = juce::jmax (1, attr (s, g, nullptr, "seqPosition").getIntValue()); t << " lorand=" << (float) (p - 1) / (float) seqLen << " hirand=" << (float) p / (float) seqLen; }
            t << "\n";
            ++regions;
        }
    }
    if (regions == 0) { log.add ("  " + dspreset.getFileName() + ": no samples listed"); return out; }
    sfz.replaceWithText (t);
    Found f;
    f.sfz = sfz; f.name = pretty (dspreset.getFileNameWithoutExtension()); f.format = "dspreset";
    juce::StringArray dropped;
    if (xml->getChildByName ("effects") != nullptr) dropped.add ("its built-in effects");
    if (xml->getChildByName ("modulators") != nullptr) dropped.add ("its modulators");
    if (xml->getChildByName ("ui") != nullptr) dropped.add ("its own knobs");
    if (xml->getChildByName ("midi") != nullptr) dropped.add ("its MIDI mappings");
    if (! dropped.isEmpty()) f.notes.add ("Decent Sampler pack: the samples and mapping were converted; " + dropped.joinIntoString (", ") + " were not (use the layer's own controls and effects)");
    out.push_back (f);
    return out;
}

// ============================================================================================ what is in a folder
std::map<juce::String, int> readMapping (const juce::String& libraryId)
{
    std::map<juce::String, int> m;
    const auto v = readJson (mappingsDir().getChildFile (safeName (libraryId) + ".json"));
    if (const auto* o = v.getDynamicObject()) for (const auto& kv : o->getProperties()) m[kv.name.toString()] = juce::jlimit (0, 127, (int) kv.value);
    return m;
}
bool writeMapping (const juce::String& libraryId, const std::map<juce::String, int>& mapping)
{
    auto o = newObject();
    for (const auto& kv : mapping) o->setProperty (juce::Identifier (kv.first), kv.second);
    mappingsDir().createDirectory();
    return mappingsDir().getChildFile (safeName (libraryId) + ".json").replaceWithText (juce::JSON::toString (juce::var (o.get())));
}

// "01- Bowed" -> "Bowed"
static juce::String withoutNumbering (const juce::String& s)
{
    int i = 0; while (i < s.length() && juce::CharacterFunctions::isDigit (s[i])) ++i;
    if (i == 0 || i > 3 || i >= s.length()) return s;
    int j = i; while (j < s.length() && (s[j] == ' ' || s[j] == '-' || s[j] == '_' || s[j] == '.')) ++j;
    if (j == i || j >= s.length() || ! (s[i] == '-' || s[i] == '_' || s[i] == '.' || s.substring (i, j).containsAnyOf ("-_."))) return s;
    return s.substring (j);
}

// the short ways players write articulations, written out
static juce::String articulationsWrittenOut (const juce::String& s)
{
    static const std::pair<const char*, const char*> known[] = {
        { "susvib", "Sustain Vibrato" }, { "sus", "Sustain" }, { "vib", "Vibrato" }, { "nonvib", "No Vibrato" }, { "pizz", "Pizzicato" }, { "spic", "Spiccato" }, { "stac", "Staccato" },
        { "trem", "Tremolo" }, { "marc", "Marcato" }, { "harm", "Harmonics" }, { "leg", "Legato" }, { "arco", "Bowed" } };
    juce::StringArray words; words.addTokens (s, " ", "");
    for (auto& w : words) for (const auto& k : known) if (w.equalsIgnoreCase (k.first)) { w = k.second; break; }
    return words.joinIntoString (" ");
}

// a folder of loose recordings, named for what it holds: the last two folders that say something. Folders that only
// sort things (the instrument families, "Samples", "Sustains") say nothing about the sound.
static juce::String folderTitle (const juce::File& dir, const juce::File& root)
{
    static const char* const sorting[] = { "samples", "samples-looped", "sustains", "sustain", "wav", "wavs", "flac", "audio", "sounds", "instruments", "programs", "sfz" };
    static const char* const families[] = { "aerophones", "idiophones", "chordophones", "membranophones", "electrophones" };
    juce::StringArray parts; parts.addTokens (dir.getRelativePathFrom (root).replaceCharacter ('\\', '/'), "/", "");
    juce::StringArray telling;
    for (const auto& p : parts)
    {
        const auto low = p.toLowerCase().trim();
        bool says = low.isNotEmpty() && low != "." && low != "..";
        for (const auto* s : sorting) if (low == s) says = false;
        for (const auto* f : families) if (low == f || low.endsWith (juce::String (" ") + f)) says = false;
        if (says) telling.add (p);
    }
    if (telling.isEmpty()) return articulationsWrittenOut (pretty (dir.getFileName()));
    const auto leaf = articulationsWrittenOut (pretty (telling[telling.size() - 1]));
    if (telling.size() == 1) return leaf;
    const auto parent = pretty (telling[telling.size() - 2]);
    return leaf.startsWithIgnoreCase (parent) ? leaf : parent + " " + leaf;
}

std::vector<Found> discover (const juce::File& folder, const LibrarySource& src, ImportReport& rep, const Progress& progress)
{
    // what the library's own record says to leave out, to call things, and where its sounds belong
    auto relOf = [&folder] (const juce::File& f) { return f.getRelativePathFrom (folder).replaceCharacter ('\\', '/'); };
    int leftOut = 0;
    auto skipped = [&] (const juce::String& rel) {
        for (const auto& pat : src.skip) if (rel.matchesWildcard (pat, true)) { ++leftOut; return true; }
        if (src.keep.isEmpty()) return false;
        for (const auto& pat : src.keep) if (rel.matchesWildcard (pat, true)) return false;
        ++leftOut; return true;                       // a list of what to keep was given, and this is not on it
    };
    auto categoryFor = [&] (const juce::String& rel) -> juce::String { for (const auto& c : src.categories) if (rel.matchesWildcard (c.first, true)) return c.second; return {}; };
    auto shownName = [&] (juce::String n) {
        n = withoutNumbering (n).trim();
        for (const auto& r : src.rename) if (n.equalsIgnoreCase (r.first)) return r.second;
        if (src.prefix.isNotEmpty() && ! n.containsIgnoreCase (src.prefix)) n = src.prefix + " " + n;
        return n;
    };

    std::vector<Found> found;
    juce::Array<juce::File> files; collect (folder, files);
    files.sort();
    // where written SFZ goes: beside the library when it is ours to write in, else in the managed folder
    const juce::File gen = src.external ? libraryDir().getChildFile ("_generated").getChildFile (src.id) : folder.getChildFile ("_atlas");
    if (gen.isDirectory()) gen.deleteRecursively();      // everything in there was written by an earlier import, and is written again now

    // ---- 1. SFZ files (minus the pieces other SFZ files include)
    juce::Array<juce::File> sfzs;
    for (const auto& f : files) if (f.hasFileExtension ("sfz")) sfzs.add (f);
    std::set<juce::String> fragments, covered;
    struct Cand { juce::File file; SfzScan scan; };
    std::vector<Cand> cands;
    int k = 0;
    for (const auto& f : sfzs)
    {
        if (progress && (++k % 20) == 0 && ! progress (0.5f * (float) k / (float) sfzs.size(), f.getFileName())) return found;
        Cand c { f, SfzLayer::scan (f.getFullPathName().toStdString(), false) };
        for (const auto& inc : c.scan.included) { const juce::File i (inc); if (i != f) fragments.insert (i.getFullPathName()); }
        for (const auto& s : c.scan.samples) covered.insert (juce::File (juce::String (s)).getFullPathName().toLowerCase());
        cands.push_back (std::move (c));
    }
    // A program that gathers others behind key switches (one key chooses sustain, another staccato...) is less use here
    // than its parts: each part that plays on its own is listed as an instrument, and the gathering program is not.
    std::set<juce::String> promoted, gathered;
    {
        std::map<juce::String, const Cand*> byPath;
        for (const auto& c : cands) byPath[c.file.getFullPathName()] = &c;
        auto gathers = [] (const Cand& c) { return c.scan.ok && c.scan.keyswitchRegions * 2 > c.scan.regions && c.scan.included.size() >= 2; };
        // what a program includes by name in its own text (the scan lists everything it reaches, pieces of pieces too)
        auto direct = [] (const Cand& c) {
            std::vector<juce::String> out;
            juce::StringArray lines; lines.addLines (c.file.loadFileAsString());
            for (auto line : lines)
            {
                line = line.upToFirstOccurrenceOf ("//", false, false);
                for (int at = line.indexOf ("#include"); at >= 0; at = line.indexOf (at + 8, "#include"))
                {
                    const auto rest = line.substring (at + 8).trimStart();
                    if (! rest.startsWithChar ('"')) continue;
                    const auto name = rest.substring (1).upToFirstOccurrenceOf ("\"", false, false);
                    if (name.isNotEmpty() && ! name.containsChar ('$')) out.push_back (c.file.getParentDirectory().getChildFile (name.replaceCharacter ('\\', '/')).getFullPathName());
                }
            }
            return out;
        };
        for (const auto& c : cands)
        {
            if (! gathers (c)) continue;
            int n = 0;
            std::vector<juce::String> mine;
            for (const auto& p : direct (c))
            {
                const auto it = byPath.find (p);
                if (it == byPath.end()) continue;
                const auto& part = *it->second;
                const bool standsAlone = part.scan.ok && part.scan.regions > 0 && ! part.scan.samples.empty() && juce::File (juce::String (part.scan.samples.front())).existsAsFile();
                if (standsAlone) { mine.push_back (p); ++n; }
            }
            if (n >= 2) { for (const auto& p : mine) promoted.insert (p); gathered.insert (c.file.getFullPathName()); }
        }
    }
    std::map<juce::String, juce::String> seen;   // name + sample set -> first path
    auto rank = [] (const juce::File& f) { const auto p = f.getFullPathName().toLowerCase(); return (p.contains ("notation") ? 2 : 0) + (p.contains ("legacy") ? 1 : 0); };
    std::stable_sort (cands.begin(), cands.end(), [&rank] (const Cand& a, const Cand& b) { return rank (a.file) < rank (b.file); });
    int fragmentsSkipped = 0, twins = 0;
    for (const auto& c : cands)
    {
        if (gathered.count (c.file.getFullPathName()) > 0) continue;
        if (fragments.count (c.file.getFullPathName()) > 0 && promoted.count (c.file.getFullPathName()) == 0) { ++fragmentsSkipped; continue; }
        if (skipped (relOf (c.file))) continue;
        if (! c.scan.ok || c.scan.regions == 0) { if (! c.scan.ok) rep.lines.add ("  " + c.file.getFileName() + ": " + juce::String (c.scan.error)); continue; }
        juce::String sig = c.file.getFileNameWithoutExtension().toLowerCase() + "|" + juce::String (c.scan.regions) + "|";
        { juce::StringArray ss; for (const auto& s : c.scan.samples) ss.add (juce::File (juce::String (s)).getFileName().toLowerCase()); sig << juce::String (ss.joinIntoString ("|").hashCode64()); }
        if (seen.count (sig) > 0) { ++twins; continue; }
        seen[sig] = c.file.getFullPathName();
        Found f;
        f.sfz = c.file; f.format = "sfz"; f.name = shownName (pretty (withoutNumbering (c.file.getFileNameWithoutExtension())));
        f.category = categoryFor (relOf (c.file));
        if (c.scan.keyswitchRegions * 2 > c.scan.regions) f.notes.add ("uses key switches to change articulation");
        found.push_back (f);
    }
    if (! gathered.empty()) rep.lines.add ("  " + juce::String ((int) gathered.size()) + " key-switched programs are listed by their articulations instead (" + juce::String ((int) promoted.size()) + " of them)");
    // ---- 1b. wave-synth instruments: one of the library's single-cycle waveforms, played as an oscillator
    int wavesMade = 0;
    for (const auto& w : src.waves)
    {
        const auto cycle = folder.getChildFile (w.file);
        if (! cycle.existsAsFile()) { rep.lines.add ("  " + w.name + ": its waveform " + w.file + " is not in the library (not made)"); continue; }
        const auto outFile = gen.getChildFile ("waves").getChildFile (safeName (w.name) + ".sfz");
        outFile.getParentDirectory().createDirectory();
        juce::String t;
        t << "// " << w.name << "\n// a wave-synth instrument made by Ghost Atlas from the single-cycle waveform " << w.file << "\n\n<global>\noscillator=on\n";
        if (w.multi >= 3) t << "oscillator_multi=" << (w.multi | 1) << "\noscillator_detune=" << juce::String (w.detune, 1) << "\n";
        t << "volume=" << (w.multi >= 3 ? -11 : -7) << "\namp_veltrack=70\n";
        if (w.shape == "pad") t << "ampeg_attack=0.7\nampeg_release=1.4\n";
        else if (w.shape == "pluck") t << "ampeg_attack=0.002\nampeg_decay=1.6\nampeg_sustain=0\nampeg_release=0.5\nfil_type=lpf_2p\ncutoff=240\nresonance=2\nfil_keytrack=50\nfil_veltrack=1800\nfileg_attack=0\nfileg_decay=0.45\nfileg_sustain=0\nfileg_depth=5400\n";
        else if (w.shape == "keys") t << "ampeg_attack=0.003\nampeg_decay=3\nampeg_sustain=25\nampeg_release=0.6\nfil_type=lpf_2p\ncutoff=1800\nfil_keytrack=60\nfil_veltrack=3600\n";
        else if (w.shape == "bell") t << "ampeg_attack=0.002\nampeg_decay=4\nampeg_sustain=0\nampeg_release=2.5\n";
        else t << "ampeg_attack=0.004\nampeg_release=0.25\n";
        t << "\n<region> sample=" << cycle.getRelativePathFrom (outFile.getParentDirectory()).replaceCharacter ('\\', '/') << " lokey=12 hikey=120 pitch_keycenter=60\n";
        if (! outFile.replaceWithText (t)) continue;
        Found f; f.sfz = outFile; f.name = w.name; f.format = "wave"; f.category = w.category;
        f.notes.add ("a wave-synth instrument: the single-cycle waveform " + cycle.getFileName() + " played as an oscillator" + (w.multi >= 3 ? ", " + juce::String (w.multi | 1) + " voices slightly apart" : juce::String()));
        found.push_back (f);
        ++wavesMade;
    }
    if (wavesMade > 0) rep.lines.add ("  " + juce::String (wavesMade) + " wave-synth instruments made from single-cycle waveforms");
    if (fragmentsSkipped > 0) rep.lines.add ("  " + juce::String (fragmentsSkipped) + " SFZ files are pieces included by others (not listed as instruments)");
    if (twins > 0) rep.lines.add ("  " + juce::String (twins) + " SFZ files repeat an instrument already listed (same name, same samples)");

    // ---- 2. Decent Sampler packs and SoundFonts
    for (const auto& f : files)
    {
        if (f.hasFileExtension ("dspreset"))
        {
            auto got = convertDecentSampler (f, gen.getChildFile ("decent-sampler").getChildFile (safeName (f.getParentDirectory().getFileName())), rep.lines);
            for (auto& g : got)
            {
                const auto scan = SfzLayer::scan (g.sfz.getFullPathName().toStdString(), false);
                for (const auto& s : scan.samples) covered.insert (juce::File (juce::String (s)).getFullPathName().toLowerCase());
                found.push_back (g);
            }
        }
        else if (f.hasFileExtension ("sf2"))
        {
            // (a SoundFont's presets are chosen, named and sorted by their own names, as files are by their paths)
            auto got = convertSoundFont (f, gen.getChildFile ("soundfont").getChildFile (safeName (f.getFileNameWithoutExtension())), rep.lines,
                                         [&] (const juce::String& presetName) { return ! skipped (presetName); });
            for (auto& g : got) { g.category = categoryFor (g.name); g.name = shownName (g.name); found.push_back (g); }
        }
    }

    // ---- 3. loose samples: every folder of recordings no SFZ points at, mapped from the names
    const auto fixedKeys = readMapping (src.id);
    std::map<juce::String, std::vector<juce::File>> byDir;
    std::set<juce::String> usedDirs;      // folders whose recordings the library's own SFZ files already play
    for (const auto& f : files) if (isAudioFile (f) && covered.count (f.getFullPathName().toLowerCase()) > 0) usedDirs.insert (f.getParentDirectory().getFullPathName());
    int cycles = 0, leftovers = 0;
    std::vector<juce::File> singles;
    for (const auto& f : files)
        if (isAudioFile (f) && covered.count (f.getFullPathName().toLowerCase()) == 0)
        {
            if (f.getSize() < 6000) { ++cycles; continue; }             // single-cycle waveforms: oscillator material, not instruments
            bool single = false;
            for (const auto& pat : src.singles) single = single || relOf (f).matchesWildcard (pat, true);
            if (single) { singles.push_back (f); continue; }
            if (! src.singles.isEmpty()) { ++leftOut; continue; }       // a library of one-shots: only the ones its record names
            if (usedDirs.count (f.getParentDirectory().getFullPathName()) > 0) { ++leftovers; continue; }   // spare takes beside the ones in use
            byDir[f.getParentDirectory().getFullPathName()].push_back (f);
        }
    if (cycles > 0) rep.lines.add ("  " + juce::String (cycles) + " single-cycle waveforms (used by wave-synth instruments, not listed one by one)");
    if (leftovers > 0) rep.lines.add ("  " + juce::String (leftovers) + " recordings sit beside the ones the library's own SFZ files play but are not used by them (left out)");
    // ---- 3a. one recording, one instrument (a texture, a drone, a single sung chord): played from C2 to C6. It sounds as
    //          recorded on the key nearest middle C that has the note it is heard to be (so a drone in G is in tune on
    //          every G, whatever octave it was recorded in); with no clear pitch, as recorded on middle C
    for (const auto& f : singles)
    {
        LooseSample s; s.file = f; readAudioFacts (s);
        if (s.frames <= 0 || s.sampleRate <= 0) continue;
        const float heard = detectNote (f);
        int root = 60;
        if (heard >= 0) { root = (int) std::lround (heard); while (root < 55) root += 12; while (root > 66) root -= 12; }
        const int cents = heard >= 0 ? (int) std::lround ((heard - std::round (heard)) * 100.0f) : 0;
        const juce::String title = shownName (pretty (f.getFileNameWithoutExtension()));
        const auto outFile = gen.getChildFile ("single").getChildFile (safeName (f.getFileNameWithoutExtension()) + ".sfz");
        outFile.getParentDirectory().createDirectory();
        juce::String t;
        t << "// " << title << "\n// One recording played across the keyboard by Ghost Atlas. The recording is not changed.\n"
          << "<control> default_path=" << f.getParentDirectory().getRelativePathFrom (outFile.getParentDirectory()).replaceCharacter ('\\', '/') << "/\n"
          << "<global> ampeg_attack=0.004 ampeg_release=0.7 amp_veltrack=70";
        {
            std::vector<LooseSample> one { s };
            const float top = peakOfSome (one);
            if (top > 1.0e-5f && top < 0.25f) t << " volume=" << juce::jlimit (0, 30, (int) std::floor (juce::Decibels::gainToDecibels (0.5f / top)));
        }
        t << "\n<region> sample=" << f.getFileName() << " pitch_keycenter=" << root;
        if (std::abs (cents) >= 4) t << " tune=" << -cents;
        t << " lokey=36 hikey=84\n";
        outFile.replaceWithText (t);
        Found one;
        one.sfz = outFile; one.name = title; one.format = "mapped"; one.category = categoryFor (relOf (f));
        one.notes.add (heard >= 0 ? "one recording, played from C2 to C6 and tuned by the note it is heard to be"
                                  : "one recording with no clear pitch, played from C2 to C6 (as recorded on middle C)");
        found.push_back (one);
    }
    k = 0;
    for (const auto& d : byDir)
    {
        const juce::File dir (d.first);
        if (progress && ! progress (0.5f + 0.5f * (float) (++k) / (float) byDir.size(), dir.getFileName())) return found;
        const auto relDir = relOf (dir);
        bool tails = false;
        { juce::StringArray parts; parts.addTokens (relDir.toLowerCase(), "/", ""); for (const auto& p : parts) tails = tails || p.startsWith ("release") || p == "rel" || p == "decays"; }
        if (tails) { rep.lines.add ("  " + relDir + ": release tails (left out of the automatic mapping)"); continue; }
        if (skipped (relDir)) continue;
        std::map<juce::String, std::vector<LooseSample>> groups;
        int named = 0;
        std::vector<LooseSample> all;
        bool inFileOrder = false;
        for (const auto& f : d.second)
        {
            auto s = parseSampleName (f); readAudioFacts (s);
            const auto said = fixedKeys.find (relOf (f));           // the owner's own word (the mapping editor) comes before any guess
            if (said != fixedKeys.end()) { s.note = said->second; s.noteFromName = false; }
            if (s.note >= 0) ++named;
            all.push_back (s);
        }
        if (named * 2 < (int) all.size() || named == 0)
        {
            // no notes in the names: ask each recording what note it is
            int heardCount = 0;
            if (all.size() <= 48)
                for (auto& s : all) if (s.note < 0) { const float h = detectNote (s.file); if (h >= 0) { s.note = juce::jlimit (0, 127, (int) std::lround (h)); ++heardCount; } }
            if ((named + heardCount) * 2 < (int) all.size())
            {
                // Nothing says what notes these are (drum hits, phrases, noises): each is given a key of its own, upwards
                // from C2 in file order, so they can be played; the mapping editor on the Library page moves them.
                std::sort (all.begin(), all.end(), [] (const LooseSample& a, const LooseSample& b) { return a.file.getFileName().compareNatural (b.file.getFileName()) < 0; });
                int key = 36, given = 0;
                for (auto& s : all) if (s.note < 0 && key <= 120) { s.note = key++; s.velocityRank = -1; s.roundRobin = -1; s.group.clear(); ++given; }
                ++rep.unmapped; inFileOrder = true;
                rep.lines.add ("  " + relDir + ": " + juce::String (given) + " recordings with no note in their names and no clear pitch were each given a key of its own, from C2 in file order (the Library page can move them)");
            }
        }
        for (auto& s : all) if (s.note >= 0) groups[s.group].push_back (s);
        // tiny groups join the biggest one (a stray name is still the same instrument)
        juce::String big; size_t bigN = 0;
        for (const auto& g : groups) if (g.second.size() > bigN) { bigN = g.second.size(); big = g.first; }
        std::vector<juce::String> small;
        for (const auto& g : groups) if (g.first != big && g.second.size() < 3) small.push_back (g.first);
        for (const auto& sname : small) { for (auto& s : groups[sname]) groups[big].push_back (s); groups.erase (sname); }

        const auto rel = dir.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
        for (auto& g : groups)
        {
            juce::String title = folderTitle (dir, folder);
            if (groups.size() > 1 && g.first.isNotEmpty())
            {   // several instruments in one folder: each is told apart by what its file names add to the folder's name
                juce::String what = g.first.trim();
                juce::StringArray tw; tw.addTokens (title, " ,", "");
                for (const auto& w : tw) if (w.length() > 2 && what.startsWithIgnoreCase (w)) what = what.substring (w.length()).trim();
                if (what.isNotEmpty()) title << " (" << what << ")";
            }
            title = shownName (title);
            const auto outFile = gen.getChildFile ("mapped").getChildFile (rel).getChildFile (safeName (groups.size() > 1 && g.first.isNotEmpty() ? g.first : dir.getFileName()) + ".sfz");
            outFile.getParentDirectory().createDirectory();
            Found f;
            const auto defaultPath = dir.getRelativePathFrom (outFile.getParentDirectory()).replaceCharacter ('\\', '/') + "/";
            const auto text = mapToSfz (g.second, defaultPath, title, f.notes);
            if (text.isEmpty()) continue;
            outFile.replaceWithText (text);
            f.sfz = outFile; f.name = title; f.format = "mapped"; f.category = categoryFor (relDir);
            if (inFileOrder) f.notes.add ("its recordings carry no note names: each has a key of its own, in file order (\"Change its mapping\" moves them)");
            f.notes.insert (0, "mapped from " + juce::String ((int) g.second.size()) + " file names");
            found.push_back (f);
        }
    }
    if (leftOut > 0) rep.lines.add ("  " + juce::String (leftOut) + " left out as the library's record asks (duplicates, variants and unpitched percussion)");
    return found;
}
} // namespace atlas::importer
