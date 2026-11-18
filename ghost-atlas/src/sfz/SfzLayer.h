// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the one doorway to the sample engine.
//
// Each of the eight layers plays an SFZ instrument through sfizz (https://sfz.tools/sfizz, BSD-2-Clause), compiled
// exactly as upstream wrote it. This header is plain C++ with no sfizz and no JUCE types in it, so the rest of the
// plug-in (C++20, JUCE) and the bridge (C++14, like sfizz itself) can be compiled with different settings.
//
// How a layer reshapes an instrument without touching the engine or the samples: the instrument's SFZ text is read
// with sfizz's own parser, and a few standard SFZ opcodes are added to its regions before the engine loads it
// (controller-driven attack / release / start, a forced loop, reverse). With every shape setting at its default
// nothing is added and the instrument plays exactly as its author made it.
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace atlas
{
// controller numbers the hooks listen on (far above anything MIDI or the ARIA extensions use)
enum { kCcAttack = 500, kCcReleaseUp = 501, kCcReleaseDown = 502, kCcStart = 503, kCcPitchUp = 504, kCcPitchDown = 505 };

struct SfzShape
{
    bool envelopeHooks = true;        // attack / release follow kCcAttack, kCcReleaseUp, kCcReleaseDown (neutral at 0)
    float attackRange = 6.0f;         // seconds added at kCcAttack = 1
    float releaseRange = 10.0f;       // seconds added at kCcReleaseUp = 1
    float startRange = 0.0f;          // seconds of sample start reachable through kCcStart (0 = no hook; costs preload memory)
    int loopMode = 0;                 // 0 as the instrument says, 1 force a sustain loop, 2 no loop (one shot)
    float loopStart = 0.35f, loopEnd = 0.94f;   // loopMode 1: where the loop sits, as fractions of each sample
    float loopCrossfade = 0.20f;      // loopMode 1: seconds
    bool reverse = false;             // play every sample backwards
    bool pitchHook = true;            // pitch follows kCcPitchUp / kCcPitchDown (neutral at 0): fine tune and vibrato, in cents
    float pitchRange = 200.0f;        // cents at kCcPitchUp = 1 (and the same downwards at kCcPitchDown = 1)
    // layer crossfades, in the keys and velocities the instrument itself hears (-1 = none): silent at the first number,
    // full level at the second for a fade in; the other way round for a fade out
    int xfKeyInLo = -1, xfKeyInHi = -1, xfKeyOutLo = -1, xfKeyOutHi = -1;
    int xfVelInLo = -1, xfVelInHi = -1, xfVelOutLo = -1, xfVelOutHi = -1;
    bool hasCrossfade() const { return xfKeyInHi >= 0 || xfKeyOutLo >= 0 || xfVelInHi >= 0 || xfVelOutLo >= 0; }
    bool isNeutral() const { return ! envelopeHooks && ! pitchHook && startRange <= 0.0f && loopMode == 0 && ! reverse && ! hasCrossfade(); }
    // the instrument exactly as its author made it (the importer and the analysis listen to this)
    static SfzShape asMade() { SfzShape s; s.envelopeHooks = false; s.pitchHook = false; return s; }
};

// what an SFZ file says, read with sfizz's parser (no audio is opened unless asked): used by the importer and its report
struct SfzScan
{
    bool ok = false;
    std::string error;
    int regions = 0, groups = 0, masters = 0, globals = 0, curves = 0, effects = 0, includes = 0;
    int loKey = 127, hiKey = 0;
    std::vector<int> keys;                     // every key that plays something when pressed (an instrument may have gaps)
    int velocityLayers = 1, roundRobins = 1;
    int loopedRegions = 0, releaseRegions = 0, oscillatorRegions = 0, keyswitchRegions = 0;
    float maxRelease = 0.0f, maxAttack = 0.0f;
    std::vector<std::string> included;         // files this one pulls in with #include (absolute paths)
    std::vector<std::string> samples;          // distinct sample files, absolute paths
    std::vector<std::string> missing;          // of those, the ones that are not on disk
    std::vector<std::string> opcodes;          // every distinct opcode name used (digits kept)
    std::vector<std::string> headers;          // every distinct header used
    std::vector<std::string> parseErrors, parseWarnings;
    std::vector<std::pair<int, std::string>> ccLabels, keyLabels;
    long long sampleBytes = 0;
};

struct SfzInfo
{
    bool ok = false;
    std::string error;
    int regions = 0, preloadedSamples = 0;
    std::vector<std::string> unknownOpcodes;   // what the engine itself did not understand (reported, never hidden)
    std::vector<int> usedCCs;
};

class SfzLayer
{
public:
    SfzLayer();
    ~SfzLayer();
    SfzLayer (const SfzLayer&) = delete;
    SfzLayer& operator= (const SfzLayer&) = delete;

    // ---- set-up (any thread but the audio thread; these allocate and read files)
    void prepare (double sampleRate, int maxBlock);
    void setVoices (int voices);
    void setQuality (int live, int offline);          // sfizz sample quality (1 fastest .. 10 best)
    bool load (const std::string& sfzPath, const SfzShape& shape);
    bool loadText (const std::string& virtualPath, const std::string& sfzText, const SfzShape& shape);
    void setOffline (bool on);                        // true: rendering faster than real time (bounces, analysis)
    // The host says it is rendering faster than real time (an export). The instrument goes into sfizz's own mode for that,
    // and from then on each note waits for its recordings before it starts, so that no note outruns the disk and is cut
    // short. Audio thread; costs nothing when nothing changes.
    void setHostOffline (bool on);
    // offline, before the first note: the recordings these notes (key, velocity 0..1) will play are read whole, so
    // that no note is cut short by a recording still on its way in
    void primeFor (const std::vector<std::pair<int, float>>& notes);
    const SfzInfo& info() const;
    const std::string& loadedText() const;            // the SFZ text the engine was actually given

    // ---- audio thread (or the offline renderer's thread)
    void noteOn (int delay, int note, float velocity01);
    void noteOff (int delay, int note, float velocity01);
    void controller (int delay, int cc, float value01);
    void pitchWheel (int delay, float bipolar);
    void aftertouch (int delay, float value01);
    void tempo (float bpm);
    void allSoundOff();
    void setTuningHz (float a4);
    void render (float* left, float* right, int frames);   // overwrites
    int activeVoices() const;

    // ---- reading a file without loading it
    static SfzScan scan (const std::string& sfzPath, bool sizeOnDisk = true);
    static bool audioFileInfo (const std::string& path, double& sampleRate, long long& frames, int& channels);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace atlas
