#include "ProjectIO.h"

namespace gslice::projectio
{
bool isProjectFile (const juce::File& f)
{
    return (f.isDirectory() && f.hasFileExtension (".ghostslice")) || f.getFileName().endsWithIgnoreCase (".gslice.json");
}
juce::File jsonOf (const juce::File& loc) { return loc.hasFileExtension (".ghostslice") ? loc.getChildFile ("project.json") : loc; }
juce::File cacheDirOf (const juce::File& loc)
{
    if (loc.hasFileExtension (".ghostslice")) return loc.getChildFile ("cache");
    return loc.getParentDirectory().getChildFile (loc.getFileName().upToLastOccurrenceOf (".gslice.json", false, true) + ".gslice-cache");
}
static juce::File projectDirOf (const juce::File& loc) { return loc.hasFileExtension (".ghostslice") ? loc : loc.getParentDirectory(); }

bool save (const Project& in, const juce::File& loc, const juce::File& audio, const SourceAudio* src, juce::String& error)
{
    Project p = in;
    if (audio != juce::File())
    {
        p.source.path = audio.getFullPathName();
        p.source.relPath = audio.getRelativePathFrom (projectDirOf (loc)).replaceCharacter ('\\', '/');
        p.source.fileName = audio.getFileName();
    }
    if (loc.hasFileExtension (".ghostslice") && ! loc.createDirectory()) { error = "Couldn't create " + loc.getFullPathName(); return false; }
    const auto json = jsonOf (loc);
    juce::TemporaryFile tmp (json);   // atomic replace: a crash never leaves half a project
    if (! tmp.getFile().replaceWithText (json::toString (p))) { error = "Couldn't write " + json.getFullPathName(); return false; }   // NOT-EXPORT: the project's own JSON (no audio)
    if (! tmp.overwriteTargetFileWithTemporary()) { error = "Couldn't write " + json.getFullPathName(); return false; }
    if (src != nullptr) writePeaks (src->peaks, src->fingerprint, cacheDirOf (loc));
    return true;
}

bool load (const juce::File& loc, Project& out, juce::String& error)
{
    const auto f = jsonOf (loc);
    if (! f.existsAsFile()) { error = "No project at " + loc.getFullPathName(); return false; }
    return json::fromString (f.loadFileAsString(), out, error);
}

juce::File resolveAudio (const Project& p, const juce::File& loc)
{
    const juce::File abs (p.source.path.isNotEmpty() && juce::File::isAbsolutePath (p.source.path) ? juce::File (p.source.path) : juce::File());
    if (abs.existsAsFile() && (p.source.fingerprint.isEmpty() || source::fingerprintFile (abs) == p.source.fingerprint)) return abs;
    if (loc != juce::File() && p.source.relPath.isNotEmpty())
    {
        const auto rel = projectDirOf (loc).getChildFile (p.source.relPath);
        if (rel.existsAsFile() && (p.source.fingerprint.isEmpty() || source::fingerprintFile (rel) == p.source.fingerprint)) return rel;
    }
    juce::Array<juce::File> near;
    if (loc != juce::File()) near.add (projectDirOf (loc));
    if (abs != juce::File()) near.add (abs.getParentDirectory());
    return source::findByFingerprint (p.source.fingerprint, p.source.fileName, near);
}

bool acceptRelink (const Project& p, const juce::File& c, juce::String& error)
{
    if (! c.existsAsFile()) { error = "No file"; return false; }
    if (p.source.fingerprint.isNotEmpty() && source::fingerprintFile (c) != p.source.fingerprint)
    { error = c.getFileName() + " is a different recording (its fingerprint doesn't match). Pick the original audio."; return false; }
    return true;
}

static juce::File peaksFile (const juce::String& fp, const juce::File& cacheDir) { return cacheDir.getChildFile ("analysis").getChildFile (fp).getChildFile ("peaks-v1.bin"); }

bool writePeaks (const PeakPyramid& pk, const juce::String& fp, const juce::File& cacheDir)
{
    if (fp.isEmpty() || pk.levels.empty()) return false;
    const auto f = peaksFile (fp, cacheDir); f.getParentDirectory().createDirectory();
    juce::MemoryOutputStream m;
    m.writeString ("GSPK1"); m.writeInt64 (pk.frames); m.writeInt (pk.channels); m.writeInt (pk.numLevels());
    for (auto& lv : pk.levels) for (auto& ch : lv) { m.writeInt64 ((juce::int64) ch.size()); for (auto& b : ch) { m.writeFloat (b.mn); m.writeFloat (b.mx); m.writeFloat (b.ms); } }
    return f.replaceWithData (m.getData(), m.getDataSize());   // NOT-EXPORT: a delete-safe drawing cache (min / max per 32+ frames, not playable audio)
}

bool readPeaks (PeakPyramid& pk, const juce::String& fp, int64 frames, int channels, const juce::File& cacheDir)
{
    const auto f = peaksFile (fp, cacheDir);
    juce::MemoryBlock mb; if (! f.loadFileAsData (mb)) return false;
    juce::MemoryInputStream in (mb, false);
    if (in.readString() != "GSPK1" || in.readInt64() != frames || in.readInt() != channels) return false;
    const int nl = in.readInt(); if (nl < 1 || nl > 40) return false;
    PeakPyramid p; p.frames = frames; p.channels = channels;
    for (int l = 0; l < nl; ++l)
    {
        p.levels.emplace_back ((size_t) channels);
        for (int c = 0; c < channels; ++c)
        {
            const auto n = in.readInt64(); if (n < 0 || n > frames + 1) return false;
            auto& v = p.levels.back()[(size_t) c]; v.resize ((size_t) n);
            for (auto& b : v) { b.mn = in.readFloat(); b.mx = in.readFloat(); b.ms = in.readFloat(); }
        }
    }
    if (in.isExhausted() == false && in.getNumBytesRemaining() != 0) return false;
    pk = std::move (p);
    return true;
}
} // namespace gslice::projectio
