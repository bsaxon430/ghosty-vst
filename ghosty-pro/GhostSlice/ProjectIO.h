#pragma once
// Project files (GHOST-SLICE-STUDY.md 2.7). Two forms, both the plugin's own format (not an export: the demo may save them):
//   MyEdit.ghostslice/            a folder: project.json + cache/analysis/<sha256>/peaks-v1.bin (delete-safe)
//   MyEdit.gslice.json            a single JSON file + a sidecar cache folder "MyEdit.gslice-cache/"
// The audio is referenced, never copied: absolute path + path relative to the project + SHA-256. On load a moved file is found
// again by its fingerprint near the project / its old folder; failing that the editor asks the user to relink it.
#include "Model.h"
#include "SourceAudio.h"

namespace gslice::projectio
{
    bool isProjectFile (const juce::File& f);              // a .ghostslice folder or a .gslice.json file
    juce::File jsonOf (const juce::File& projectLocation);  // the project.json inside a folder (or the file itself)
    juce::File cacheDirOf (const juce::File& projectLocation);
    // save: fills source.path / relPath from `audio` (the file in use). Creates the folder. NOT an export.
    bool save (const Project& p, const juce::File& location, const juce::File& audio, const SourceAudio* src, juce::String& error);
    bool load (const juce::File& location, Project& out, juce::String& error);
    // where the project's audio is now: the saved path, then relative to the project, then a fingerprint search nearby. {} = relink.
    juce::File resolveAudio (const Project& p, const juce::File& location);
    // a relinked file must carry the same fingerprint (true) - a different file is refused (false + reason)
    bool acceptRelink (const Project& p, const juce::File& candidate, juce::String& error);

    // peak pyramid cache (delete-safe)
    bool writePeaks (const PeakPyramid& pk, const juce::String& fingerprint, const juce::File& cacheDir);
    bool readPeaks (PeakPyramid& pk, const juce::String& fingerprint, int64 frames, int channels, const juce::File& cacheDir);
}
