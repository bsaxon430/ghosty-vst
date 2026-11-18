// GHOST SLICE analysis, part 3: the versioned, delete-safe cache under cache/analysis/<sha256>/, the full analyse() and the
// background worker. Files (all NOT-EXPORT: analysis data, never audio):
//   onsets-v1-<material>.bin   the onset events of that material profile
//   tempo-v2.json              BPM, beat / downbeat, confidence (readable)
//   pitch-v1.bin               the f0 track (material free)
//   notes-v1-<material>.bin    notes + key + median f0 (the note splits use the material's onsets)
//   syllables-v1.bin           the syllables (phase 16; material free)
//   chords-v1.bin              the chord track (phase 17; material free; Chords.h)
// Each file starts with a tag, its version and the source length; any mismatch = recompute (and overwrite).
#include "AnalysisInternal.h"
#include "SourceAudio.h"
#include "Syllables.h"
#include "Chords.h"

namespace gslice::analysis
{
juce::String Result::digest() const
{
    Hasher h; h.add (fingerprint).add ((int) material).add (onsets.digest()).add (tempo.bpm).add (tempo.confidence).add (tempo.firstBeat).add (tempo.downbeat).add (tempo.bars)
        .add (pitch.digest()).add ((int) notes.size());
    for (auto& n : notes) h.add (n.s0).add (n.s1).add (n.pitch).add (n.driftCents).add (n.vibratoCents).add (n.vibratoHz).add (n.confidence).add (n.loudness);
    h.add (key.tonic).add (key.minor).add (key.confidence).add (medianF0).add (analysis::digest (syllables));
    if (chords != nullptr) h.add (chords->digest());
    return h.hex();
}

namespace cache
{
juce::File dirFor (const juce::File& cacheDir, const juce::String& fp) { return cacheDir.getChildFile ("analysis").getChildFile (fp); }

static bool put (const juce::File& f, const juce::MemoryOutputStream& m)
{
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);
    if (! tmp.getFile().replaceWithData (m.getData(), m.getDataSize())) return false;   // NOT-EXPORT: an analysis cache file (no audio)
    return tmp.overwriteTargetFileWithTemporary();
}
static bool get (const juce::File& f, juce::MemoryBlock& mb) { return f.existsAsFile() && f.loadFileAsData (mb); }
static juce::String matTag (Material m) { return juce::String (materialName (m)).toLowerCase(); }

bool writeOnsets (const juce::File& dir, const OnsetSet& o, int64 frames)
{
    juce::MemoryOutputStream m;
    m.writeString ("GSON"); m.writeInt (kOnsetVersion); m.writeInt64 (frames); m.writeInt ((int) o.asked); m.writeInt ((int) o.used);
    m.writeInt ((int) o.events.size());
    for (auto& e : o.events) { m.writeInt64 (e.pos); m.writeFloat (e.strength); m.writeFloat (e.flatness); m.writeByte ((char) e.flags); m.writeInt (e.group); }
    return put (dir.getChildFile ("onsets-v" + juce::String (kOnsetVersion) + "-" + matTag (o.asked) + ".bin"), m);
}
bool readOnsets (const juce::File& dir, Material asked, int64 frames, OnsetSet& out)
{
    juce::MemoryBlock mb; if (! get (dir.getChildFile ("onsets-v" + juce::String (kOnsetVersion) + "-" + matTag (asked) + ".bin"), mb)) return false;
    juce::MemoryInputStream in (mb, false);
    if (in.readString() != "GSON" || in.readInt() != kOnsetVersion || in.readInt64() != frames || in.readInt() != (int) asked) return false;
    OnsetSet o; o.asked = asked; o.used = (Material) juce::jlimit (0, 3, in.readInt());
    const int cnt = in.readInt(); if (cnt < 0 || cnt > 10000000) return false;
    for (int i = 0; i < cnt; ++i)
    {
        Event e; e.pos = in.readInt64(); e.strength = in.readFloat(); e.flatness = in.readFloat(); e.flags = (uint8_t) in.readByte(); e.group = in.readInt();
        if (e.pos < 0 || e.pos > frames) return false;
        o.events.push_back (e);
    }
    if (in.getNumBytesRemaining() != 0) return false;
    out = std::move (o); return true;
}

static juce::var num (double v) { char b[40]; std::snprintf (b, sizeof (b), "%.17g", v); return juce::var (juce::String (b)); }
static double dbl (const juce::var& v) { return v.isString() ? std::strtod (v.toString().toRawUTF8(), nullptr) : (double) v; }

bool writeTempo (const juce::File& dir, const Tempo& t, int64 frames)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "ghostslice-tempo"); o->setProperty ("version", kTempoVersion); o->setProperty ("frames", (juce::int64) frames);
    o->setProperty ("bpm", num (t.bpm)); o->setProperty ("confidence", num (t.confidence)); o->setProperty ("firstBeat", num (t.firstBeat));
    o->setProperty ("downbeat", num (t.downbeat)); o->setProperty ("bars", t.bars);
    o->setProperty ("octaveRatio", num (t.octaveRatio));
    juce::MemoryOutputStream m; m << juce::JSON::toString (juce::var (o), false);
    return put (dir.getChildFile ("tempo-v" + juce::String (kTempoVersion) + ".json"), m);
}
bool readTempo (const juce::File& dir, int64 frames, Tempo& out)
{
    const auto f = dir.getChildFile ("tempo-v" + juce::String (kTempoVersion) + ".json"); if (! f.existsAsFile()) return false;
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (v["format"].toString() != "ghostslice-tempo" || (int) v["version"] != kTempoVersion || (int64) (juce::int64) v["frames"] != frames) return false;
    Tempo t; t.bpm = dbl (v["bpm"]); t.confidence = dbl (v["confidence"]); t.firstBeat = dbl (v["firstBeat"]); t.downbeat = dbl (v["downbeat"]); t.bars = (int) v["bars"];
    t.octaveRatio = v.hasProperty ("octaveRatio") ? dbl (v["octaveRatio"]) : -1.0;
    if (! std::isfinite (t.bpm) || t.bpm < 0 || t.bpm > 1000) return false;
    out = t; return true;
}

bool writePitch (const juce::File& dir, const PitchTrack& t, int64 frames)
{
    juce::MemoryOutputStream m;
    m.writeString ("GSPT"); m.writeInt (kPitchVersion); m.writeInt64 (frames); m.writeDouble (t.sr); m.writeInt (t.hop); m.writeInt (t.win);
    m.writeDouble (t.fmin); m.writeDouble (t.fmax); m.writeInt (t.frames());
    for (int i = 0; i < t.frames(); ++i) { m.writeFloat (t.f0[(size_t) i]); m.writeFloat (t.voicedProb[(size_t) i]); m.writeFloat (t.rmsDb[(size_t) i]); }
    return put (dir.getChildFile ("pitch-v" + juce::String (kPitchVersion) + ".bin"), m);
}
bool readPitch (const juce::File& dir, int64 frames, PitchTrack& out)
{
    juce::MemoryBlock mb; if (! get (dir.getChildFile ("pitch-v" + juce::String (kPitchVersion) + ".bin"), mb)) return false;
    juce::MemoryInputStream in (mb, false);
    if (in.readString() != "GSPT" || in.readInt() != kPitchVersion || in.readInt64() != frames) return false;
    PitchTrack t; t.sr = in.readDouble(); t.hop = in.readInt(); t.win = in.readInt(); t.fmin = in.readDouble(); t.fmax = in.readDouble();
    const int F = in.readInt(); if (F < 0 || t.hop <= 0 || (int64) F > frames / t.hop + 2) return false;
    t.f0.resize ((size_t) F); t.voicedProb.resize ((size_t) F); t.rmsDb.resize ((size_t) F);
    for (int i = 0; i < F; ++i) { t.f0[(size_t) i] = in.readFloat(); t.voicedProb[(size_t) i] = in.readFloat(); t.rmsDb[(size_t) i] = in.readFloat(); }
    if (in.getNumBytesRemaining() != 0) return false;
    out = std::move (t); return true;
}

bool writeNotes (const juce::File& dir, Material mat, const std::vector<Note>& notes, const Key& k, double medianF0, int64 frames)
{
    juce::MemoryOutputStream m;
    m.writeString ("GSNT"); m.writeInt (kPitchVersion); m.writeInt64 (frames); m.writeInt ((int) mat);
    m.writeInt ((int) notes.size());
    for (auto& n : notes) { m.writeInt64 (n.s0); m.writeInt64 (n.s1); m.writeDouble (n.pitch); m.writeDouble (n.driftCents); m.writeDouble (n.vibratoCents); m.writeDouble (n.vibratoHz); m.writeFloat (n.confidence); m.writeFloat (n.loudness); }
    m.writeInt (k.tonic); m.writeBool (k.minor); m.writeDouble (k.confidence); for (double r : k.r) m.writeDouble (r);
    m.writeDouble (medianF0);
    return put (dir.getChildFile ("notes-v" + juce::String (kPitchVersion) + "-" + matTag (mat) + ".bin"), m);
}
bool readNotes (const juce::File& dir, Material mat, int64 frames, std::vector<Note>& out, Key& key, double& medianF0)
{
    juce::MemoryBlock mb; if (! get (dir.getChildFile ("notes-v" + juce::String (kPitchVersion) + "-" + matTag (mat) + ".bin"), mb)) return false;
    juce::MemoryInputStream in (mb, false);
    if (in.readString() != "GSNT" || in.readInt() != kPitchVersion || in.readInt64() != frames || in.readInt() != (int) mat) return false;
    const int cnt = in.readInt(); if (cnt < 0 || cnt > 1000000) return false;
    std::vector<Note> notes;
    for (int i = 0; i < cnt; ++i)
    {
        Note n; n.s0 = in.readInt64(); n.s1 = in.readInt64(); n.pitch = in.readDouble(); n.driftCents = in.readDouble(); n.vibratoCents = in.readDouble(); n.vibratoHz = in.readDouble();
        n.confidence = in.readFloat(); n.loudness = in.readFloat(); notes.push_back (n);
    }
    Key k; k.tonic = in.readInt(); k.minor = in.readBool(); k.confidence = in.readDouble(); for (auto& r : k.r) r = in.readDouble();
    const double mf = in.readDouble();
    if (in.getNumBytesRemaining() != 0 || k.tonic < -1 || k.tonic > 11) return false;
    out = std::move (notes); key = k; medianF0 = mf; return true;
}
bool writeSyllables (const juce::File& dir, const std::vector<Syllable>& s, int64 frames)
{
    juce::MemoryOutputStream m;
    m.writeString ("GSSY"); m.writeInt (kSyllableVersion); m.writeInt64 (frames); m.writeInt ((int) s.size());
    for (auto& x : s) { m.writeInt64 (x.start); m.writeInt64 (x.vowelStart); m.writeInt64 (x.nucleus); m.writeInt64 (x.vowelEnd); m.writeInt64 (x.end); m.writeFloat (x.confidence); }
    return put (dir.getChildFile ("syllables-v" + juce::String (kSyllableVersion) + ".bin"), m);
}
bool readSyllables (const juce::File& dir, int64 frames, std::vector<Syllable>& out)
{
    juce::MemoryBlock mb; if (! get (dir.getChildFile ("syllables-v" + juce::String (kSyllableVersion) + ".bin"), mb)) return false;
    juce::MemoryInputStream in (mb, false);
    if (in.readString() != "GSSY" || in.readInt() != kSyllableVersion || in.readInt64() != frames) return false;
    const int cnt = in.readInt(); if (cnt < 0 || cnt > 10000000) return false;
    std::vector<Syllable> s;
    for (int i = 0; i < cnt; ++i)
    {
        Syllable x; x.start = in.readInt64(); x.vowelStart = in.readInt64(); x.nucleus = in.readInt64(); x.vowelEnd = in.readInt64(); x.end = in.readInt64(); x.confidence = in.readFloat();
        if (x.start < 0 || x.end > frames || x.start > x.vowelStart || x.vowelStart > x.vowelEnd || x.vowelEnd > x.end) return false;
        s.push_back (x);
    }
    if (in.getNumBytesRemaining() != 0) return false;
    out = std::move (s); return true;
}
bool writeChords (const juce::File& dir, const ChordTrack& t, int64 frames)
{
    juce::MemoryOutputStream m;
    m.writeString ("GSCH"); m.writeInt (kChordVersion); m.writeInt64 (frames); m.writeInt ((int) t.chords.size());
    for (auto& c : t.chords) { m.writeInt64 (c.s0); m.writeInt64 (c.s1); m.writeInt (c.root); m.writeInt ((int) c.quality); m.writeFloat (c.confidence); }
    return put (dir.getChildFile ("chords-v" + juce::String (kChordVersion) + ".bin"), m);
}
bool readChords (const juce::File& dir, int64 frames, ChordTrack& out)
{
    juce::MemoryBlock mb; if (! get (dir.getChildFile ("chords-v" + juce::String (kChordVersion) + ".bin"), mb)) return false;
    juce::MemoryInputStream in (mb, false);
    if (in.readString() != "GSCH" || in.readInt() != kChordVersion || in.readInt64() != frames) return false;
    const int cnt = in.readInt(); if (cnt < 0 || cnt > 10000000) return false;
    ChordTrack t; t.frames = frames;
    int64 prev = 0;
    for (int i = 0; i < cnt; ++i)
    {
        Chord c; c.s0 = in.readInt64(); c.s1 = in.readInt64(); c.root = in.readInt(); const int q = in.readInt(); c.confidence = in.readFloat();
        if (c.s0 != prev || c.s1 <= c.s0 || c.s1 > frames || c.root < -1 || c.root > 11 || q < 0 || q > 5) return false;
        c.quality = (ChordQuality) q; prev = c.s1; t.chords.push_back (c);
    }
    if (in.getNumBytesRemaining() != 0 || (cnt > 0 && prev != frames)) return false;
    out = std::move (t); return true;
}
} // namespace cache

// ---------------------------------------------------------------- the whole analysis
std::shared_ptr<const Result> analyse (const SourceAudio& src, Material m, const juce::File& cacheDir, const std::atomic<bool>* cancel)
{
    auto r = std::make_shared<Result>();
    r->fingerprint = src.fingerprint; r->material = m;
    const auto x = mono (src.audio);
    const int64 n = (int64) x.size(); const double sr = src.sampleRate;
    const juce::File dir = cacheDir != juce::File() && src.fingerprint.isNotEmpty() ? cache::dirFor (cacheDir, src.fingerprint) : juce::File();
    const bool useCache = dir != juce::File();
    auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };
    bool all = useCache;
    if (! useCache || ! cache::readOnsets (dir, m, n, r->onsets))
    {
        all = false; r->onsets = detectOnsets (x.data(), n, sr, m, cancel); if (cancelled()) return nullptr;
        if (useCache) cache::writeOnsets (dir, r->onsets, n);
    }
    if (! useCache || ! cache::readTempo (dir, n, r->tempo))
    {
        all = false; r->tempo = estimateTempo (x.data(), n, sr, cancel); if (cancelled()) return nullptr;
        if (useCache) cache::writeTempo (dir, r->tempo, n);
    }
    bool havePitch = useCache && cache::readPitch (dir, n, r->pitch);
    if (! havePitch)
    {
        all = false; r->pitch = trackPitch (x.data(), n, sr, 50.0, 1500.0, cancel); if (cancelled()) return nullptr;
        if (useCache) cache::writePitch (dir, r->pitch, n);
    }
    if (! useCache || ! cache::readNotes (dir, m, n, r->notes, r->key, r->medianF0))
    {
        all = false;
        r->notes = segmentNotes (r->pitch, r->onsets.events);
        r->key = estimateKey (r->notes);
        std::vector<double> f; for (float v : r->pitch.f0) if (v > 0) f.push_back (v);
        r->medianF0 = detail::median (f);
        if (useCache) cache::writeNotes (dir, m, r->notes, r->key, r->medianF0, n);
    }
    if (! useCache || ! cache::readSyllables (dir, n, r->syllables))
    {
        all = false; r->syllables = detectSyllables (x.data(), n, sr, &r->pitch, cancel); if (cancelled()) return nullptr;
        if (useCache) cache::writeSyllables (dir, r->syllables, n);
    }
    {   // the chord track (material free) and the key with chords
        auto ct = std::make_shared<ChordTrack>();
        if (! useCache || ! cache::readChords (dir, n, *ct))
        {
            all = false; *ct = detectChords (x.data(), n, sr, {}, cancel); if (cancelled()) return nullptr;
            if (useCache) cache::writeChords (dir, *ct, n);
        }
        r->chords = ct;
        r->noteKey = r->key;
        r->key = estimateKeyWithChords (r->notes, *ct);
    }
    r->fromCache = all;
    return r;
}

// ---------------------------------------------------------------- worker
Analyzer::~Analyzer() { onDone = nullptr; stop(); }

void Analyzer::stop()
{
    if (cancelFlag) cancelFlag->store (true);
    if (worker.joinable()) worker.join();
    running.store (false);
}

void Analyzer::start (std::shared_ptr<const SourceAudio> src, Material mat, const juce::File& cacheDir)
{
    stop();
    { std::lock_guard<std::mutex> l (m); if (res != nullptr && (src == nullptr || res->fingerprint != src->fingerprint)) res = nullptr; }
    if (src == nullptr) return;
    auto flag = std::make_shared<std::atomic<bool>> (false);
    cancelFlag = flag; running.store (true);
    worker = std::thread ([this, src, mat, cacheDir, flag]
    {
        auto r = analyse (*src, mat, cacheDir, flag.get());
        if (! flag->load() && r != nullptr) { std::lock_guard<std::mutex> l (m); res = r; }
        running.store (false);
        if (! flag->load() && r != nullptr && onDone) onDone();
    });
}

bool Analyzer::waitIdle (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (running.load()) { if (juce::Time::getMillisecondCounterHiRes() > end) return false; juce::Thread::sleep (5); }
    return true;
}
} // namespace gslice::analysis
