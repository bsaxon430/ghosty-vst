// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the importer's working parts: finding what a folder can become, and writing SFZ for the things that
// are not SFZ yet (loose samples named by note, SoundFonts, Decent Sampler packs). The recordings are never changed:
// everything written goes into an "_atlas" folder beside them and only points at them.
#pragma once
#include "Library.h"
#include "SfzLayer.h"

namespace atlas::importer
{
struct Found
{
    juce::File sfz;
    juce::String name, format, category;
    juce::StringArray notes;
};

juce::String folderSignature (const juce::File& folder);
bool copyFolder (const juce::File& from, const juce::File& to, const Progress& progress);
void readSourceRecord (const juce::File& folder, LibrarySource& src);
std::vector<Found> discover (const juce::File& folder, const LibrarySource& src, ImportReport& rep, const Progress& progress);

// ---- loose samples -> SFZ
struct LooseSample
{
    juce::File file;
    int note = -1;          // MIDI note from the file name (C4 = 60), or from the file's own root-note tag
    int velocityRank = -1;  // ordering among velocity layers (-1: none in the name)
    int roundRobin = -1;
    juce::String group;     // what is left of the name: the articulation
    double sampleRate = 0; juce::int64 frames = 0; int channels = 0;
    juce::int64 loopStart = -1, loopEnd = -1;
    bool noteFromName = false;
};
// reads a file name the way sample libraries write them: VlnEns_Pizz_F#3_v2_rr1, glass3_A#4_Slow_1_Main, Piano.ff.C4 ...
LooseSample parseSampleName (const juce::File& file);
// the SFZ text for one group of samples (already parsed); empty if they cannot be mapped
juce::String mapToSfz (std::vector<LooseSample> samples, const juce::String& defaultPath, const juce::String& title, juce::StringArray& notes);

// ---- SoundFont 2 and Decent Sampler -> SFZ (written under outDir). Each returns the SFZ files it wrote.
// (wanted: asked for each preset by name before anything of it is written; none given means every preset)
std::vector<Found> convertSoundFont (const juce::File& sf2, const juce::File& outDir, juce::StringArray& log, const std::function<bool (const juce::String&)>& wanted = {});
std::vector<Found> convertDecentSampler (const juce::File& dspreset, const juce::File& outDir, juce::StringArray& log);

// What the owner has decided about a library's loose recordings (the mapping editor): recording (its path inside the
// library's folder) -> the key it sits on. Kept outside the library's folder, so reading the library again keeps it.
std::map<juce::String, int> readMapping (const juce::String& libraryId);
bool writeMapping (const juce::String& libraryId, const std::map<juce::String, int>& mapping);

juce::AudioFormatManager& audioFormats();
bool isAudioFile (const juce::File& f);
} // namespace atlas::importer
