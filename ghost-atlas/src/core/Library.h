// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the sound library: every playable instrument that has been imported, where it came from, under what
// licence, and what it sounds like (the analysis MUTATE works from). Stored in one JSON file beside the Library folder.
//
// An instrument here is raw material: one SFZ file (the library's own, or one the importer wrote for loose samples, a
// SoundFont or a Decent Sampler pack) that points at the original recordings, which are never changed. A preset is a
// different thing (core/Preset.h): the finished, layered sound built from instruments.
#pragma once
#include "Common.h"

namespace atlas
{
// what the analysis heard (core/Analysis.cpp). All levels in dB, shares 0..1.
struct Features
{
    bool valid = false;
    float loudnessDb = -30.0f;     // of a held four-note chord at velocity 0.8 (K-weighted style RMS), for level matching
    float peakDb = -12.0f;
    float attackMs = 5.0f;         // to 90 % of the top
    float sustain = 1.0f;          // level 2.5 s into a held note against its top (pads near 1, plucks near 0)
    float releaseMs = 200.0f;      // to -40 dB after the key comes up
    float centroidHz = 1000.0f;
    float brightness = 0.5f;       // how much lies above 2 kHz against below it (0 = 35 dB less, 1 = as much)
    float low = 0.2f, mid = 0.5f, high = 0.25f, air = 0.05f;   // energy below 200 Hz, to 2 kHz, to 8 kHz, above
    float flatness = 0.1f;         // 0 = pure tones, 1 = noise
    float ring = 0.0f;             // dB: how far its sharpest narrow band (1.5 to 10 kHz) stands above its neighbours in a chord
    float harmonicity = 0.8f;      // how clearly pitched
    float tuneCents = 0.0f;        // how far from concert pitch on average
    float pitchSpread = 0.0f;      // cents, across the notes probed
    float width = 0.2f;            // side / (mid + side)
    float monoLossDb = 0.0f;       // what folding to mono costs
    float movement = 0.1f;         // how much the tone changes while held
    float loopClick = 0.0f;        // 0 clean .. 1 an audible loop seam
    float transient = 0.0f;        // how much the first 40 ms stands above the body
    float velocityDb = 12.0f;      // how much louder velocity 1.0 is than 0.3
    std::array<float, 24> bands {};   // dB per band of the chord (relative to its total), 24 bands 40 Hz .. 16 kHz
    std::array<float, 12> mfcc {};    // Samplebrain's MFCC of the held note (its 12 filters), averaged over its blocks
    std::array<float, 12> mfccDev {};
    std::array<float, 16> fft {};     // Samplebrain's FFT bins of the held note, pooled to 16
    int probeNote = 60;

    juce::var toVar() const;
    static Features fromVar (const juce::var&);
};

struct Instrument
{
    juce::String id;               // "<library id>:<path of the sfz inside the library, without .sfz>"
    juce::String name;
    juce::String libraryId, libraryName;
    juce::String sfz;              // path of the SFZ relative to the library's folder
    juce::String format;           // sfz | mapped (loose samples) | sf2 | dspreset | wave (single-cycle oscillator)
    juce::String category;         // one of the nineteen
    juce::StringArray tags;
    juce::String author, license, source, restrictions;
    int regions = 0, samples = 0, loKey = 0, hiKey = 127, velocityLayers = 1, roundRobins = 1;
    juce::String keyMap;           // which of the 128 keys play something (32 hex digits; "" = all of loKey..hiKey)
    bool looped = false, hasReleaseSamples = false;
    bool plays (int key) const;    // does this key sound?
    int nearestKey (int key) const;
    juce::int64 bytes = 0, added = 0;
    juce::StringArray unsupported; // SFZ opcodes the engine did not understand (shown, never hidden)
    juce::StringArray notes;       // anything else the importer wants the user to know
    Features f;
    std::vector<float> preview;    // waveform outline of a held note, 0..1
    bool favourite = false, hidden = false;

    juce::var toVar() const;
    static Instrument fromVar (const juce::var&);
};

struct LibrarySource
{
    juce::String id, name, folder; // folder: inside Library/ unless external is set (then an absolute path)
    juce::String author, credit, license, licenseUrl, source, restrictions, version;
    juce::String category;         // what its sounds are, when the library's record says so (used when a name does not)
    juce::String signature;        // what its files look like (names + sizes), to spot the same library dropped twice
    juce::String record;           // a fingerprint of its record (ATLAS-SOURCE.json): when the record changes, it is indexed again
    // curation, from the library's record (ATLAS-SOURCE.json); read again at every index, never stored:
    juce::StringArray skip;                                          // wildcards against a path inside the library: left out
    juce::StringArray keep;                                          // when given, only what matches one of these is listed (a SoundFont's presets by name)
    juce::StringArray singles;                                       // wildcards against a path: each recording that matches is an instrument of its own
    std::vector<std::pair<juce::String, juce::String>> rename;       // a name as found -> the name to show
    std::vector<std::pair<juce::String, juce::String>> categories;   // wildcard against a path inside the library -> category
    juce::String prefix;                                             // put before names that do not already carry it
    // wave-synth instruments to make from the library's single-cycle waveforms (each becomes an SFZ that plays the
    // cycle as an oscillator): shape is hold | pad | pluck | keys | bell
    struct Wave { juce::String name, file, shape, category; int multi = 1; float detune = 0.0f; };
    std::vector<Wave> waves;
    bool external = false;
    juce::int64 bytes = 0, added = 0;
    int instruments = 0;

    juce::var toVar() const;
    static LibrarySource fromVar (const juce::var&);
};

struct ImportReport
{
    int added = 0, updated = 0, duplicates = 0, failed = 0, unmapped = 0;
    juce::StringArray lines;           // what happened, in order, for the screen and the log
    juce::StringArray instrumentIds;
    juce::String text() const { return lines.joinIntoString ("\n"); }
};

using Progress = std::function<bool (float fraction, const juce::String& what)>;   // return false to stop

namespace importer { struct Found; }

class Library
{
public:
    Library() = default;

    // ---- the database
    bool load();                                   // from databaseFile(); false if there is none yet
    bool save() const;
    std::vector<Instrument> all() const;
    int size() const;
    bool find (const juce::String& id, Instrument& out) const;
    bool findLoose (const juce::String& id, Instrument& out) const;    // exact id, else same library + same name (a library re-fetched under a newer date)
    juce::File sfzFile (const Instrument&) const;
    juce::String sfzPath (const juce::String& id) const;               // "" if unknown or the file is gone
    std::vector<LibrarySource> sources() const;
    void setFavourite (const juce::String& id, bool on);
    void setCategory (const juce::String& id, const juce::String& category);
    void put (const Instrument&);                  // add or replace
    void remove (const juce::String& id);
    bool relink (const juce::String& libraryId, const juce::File& newFolder);   // a library whose files moved
    juce::StringArray missing() const;             // instruments whose SFZ is not on disk

    // ---- importing (any thread but the audio thread; these read and may copy files)
    // A folder dropped by the user. copyIn: copy it into the managed Library folder first (the originals are left alone);
    // otherwise it is used where it is. Analysis is run for what was added.
    ImportReport importFolder (const juce::File& folder, bool copyIn, const Progress& progress = {});
    // Index everything under the managed Library folder (what fetch_library.py put there, or after a reinstall).
    ImportReport indexManagedLibrary (const Progress& progress = {}, bool analyse = true);
    // Analyse instruments that have not been analysed yet (or all of them).
    int analyseMissing (const Progress& progress = {}, bool everything = false);

    // An instrument made here (REIMAGINE): its SFZ already lies in the library's folder under Library/. It is scanned,
    // listened to and added (and the library's own record with it, the first time).
    bool addMade (LibrarySource src, const juce::File& sfz, const juce::String& name, const juce::String& category, const juce::StringArray& notes, Instrument& out, juce::String& error);

    // ---- the mapping editor: where each loose recording of a mapped instrument sits, and moving them
    struct Mapped { juce::String file; int note = 60; };              // a recording (its path inside the library's folder) and its key
    std::vector<Mapped> mappingOf (const Instrument&) const;           // empty unless the instrument was mapped from loose recordings
    // moves recordings to other keys: remembered for the library, which is then read again (its instruments are listened to afresh)
    ImportReport remap (const Instrument&, const std::vector<Mapped>& changed, const Progress& progress = {});

    // the licence manifest as text (name, source, author, licence, restrictions, where it is on disk)
    juce::String manifestText() const;

private:
    ImportReport indexFolder (LibrarySource src, const juce::File& folder, const Progress& progress, float p0, float p1, bool analyse);
    bool addFound (const LibrarySource& src, const juce::File& folder, const importer::Found& fd, bool analyse, ImportReport& rep, juce::int64& totalBytes);
    juce::File folderOf (const LibrarySource&) const;
    int indexOfLocked (const juce::String& id) const;

    mutable juce::CriticalSection lock;
    std::vector<Instrument> items;
    std::vector<LibrarySource> libs;
};

// helpers shared with the importer's converters
juce::String guessCategory (const juce::String& name, const juce::String& context, const juce::String& libraryCategory = {});
juce::String refineCategory (const juce::String& category, const Features& f);   // what was heard can correct what the name suggested
juce::StringArray guessTags (const juce::String& name, const Features& f);
juce::String keysToMap (const std::vector<int>& keys);
} // namespace atlas
