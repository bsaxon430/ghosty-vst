// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - MUTATE: a new version of a sound, made by choosing layers that work together, setting them up for
// the part each one plays, and then listening to the result and rejecting what does not hold up.
//
// How it decides (the order matters):
//   1. Timbre, with Samplebrain's own measure (core/Timbre.h): how alike two instruments sound. This only makes the
//      shortlist - how far from the present sound to look. SUBTLE looks nearby, EVOLVE further off, WILD far away.
//   2. Fit, which is a different question from likeness: does this instrument do the job the layer needs (the body
//      under a pluck, the slow air behind a pad, the sparkle on top), does it sit where the other layers leave room
//      (its energy against theirs, band by band), is it in the same register and tuning, will it fold to mono.
//      Two to four layers that fit are preferred to many that merely resemble.
//   3. Each chosen layer is set up for its part: level matched by measured loudness, tuned to agree with the main
//      sound, filtered out of the others' way, given the envelope, width and sends its part calls for.
//   4. The result is played through the real engine (single notes low, middle and high, soft and hard, and a chord)
//      and measured. What can be put right is put right (level, low end, a buried main sound, width); what cannot
//      (loop seams, a ring that will not go) rejects the candidate, and another is tried.
//   5. The survivor's master level is set so that it is as loud as the original, so the two can be compared fairly.
#pragma once
#include "Library.h"
#include "Preset.h"
#include "Timbre.h"
#include "../engine/Engine.h"

namespace atlas
{
enum class MutateMode { Subtle, Evolve, Wild };

struct QualityCheck { juce::String name; bool passed = true; juce::String found; };

// one sound, played through the engine over a fixed set of notes and measured
struct Heard
{
    bool ok = false;
    float peakDb = -120.0f;                         // the highest peak of everything played
    float loudnessDb = -120.0f, onsetDb = -120.0f;  // the chord over its three seconds, and its loudest 400 ms
    float lowShare = 0.0f;                          // of the chord's energy, how much lies under 150 Hz
    float monoLossDb = 0.0f;                        // what folding the chord to mono costs
    float ringDb = 0.0f;                            // how far one narrow band between 1.5 and 10 kHz stands above its neighbours
    float tuneCents = 0.0f, tuneStrength = 0.0f;    // the middle note against concert pitch
    float registerSpreadDb = 0.0f;                  // loudest against quietest of the low, middle and high notes
    float touchDb = 0.0f;                           // a hard note against a soft one
    float seams = 0.0f;                             // 0 clean .. 1 loop seams can be heard while the chord is held
    bool holds = true;                              // the chord is still sounding at its end (a pad), not struck and fading (a pluck, a bell)
    std::array<float, 24> bands {};                 // the chord's energy by band, as power (not dB)
    juce::AudioBuffer<float> audio;
    int midNote = 60;
};

struct Mutation
{
    bool ok = false;
    Preset preset;                       // the new sound, as loud as the original
    juce::StringArray changes;           // what was done, in plain words
    std::vector<QualityCheck> checks;    // what was listened for, and what was found
    juce::String failure;                // why nothing came of it (when ok is false)
    int tried = 0, rejected = 0;
    float originalLoudnessDb = 0.0f, loudnessDb = 0.0f;
    float originalTrimDb = 0.0f;         // 0 or less: what the original is turned down by while the two are compared, when the new sound
                                         // could not be brought all the way up to it without clipping (the original itself is never changed)
    juce::String madeInstrument, madeFolder;   // REIMAGINE only: the instrument it added to the library, and where its files are
};

class Mutator
{
public:
    Mutator (const Library& library, Engine::Resolver resolver);
    ~Mutator();

    Mutation mutate (const Preset& original, MutateMode mode, juce::uint32 seed, const Progress& progress = {});

    // ---- the parts (the experiment and the checks use them one by one)
    struct Pick { Instrument inst; juce::String role; float likeness = 0.0f, fit = 0.0f; juce::String why; };
    // instruments for one part beside the layers already in `bed`: shortlisted by likeness to `like` (how far depends on
    // the mode), then ranked by fit. Best first.
    std::vector<Pick> candidates (const Preset& bed, const juce::String& role, const Instrument* like, MutateMode mode, const juce::StringArray& avoid = {}) const;
    void place (Preset& p, int layer, const Instrument& inst, const juce::String& role, juce::Random& rng) const;   // a layer set up for its part
    Heard listen (const Preset& p, int soloLayer = -1) const;                // master effects off, so the true peak is seen
    std::vector<QualityCheck> judge (const Heard& mix, const Heard* mainAlone, const Preset& p) const;
    bool refine (Preset& p, juce::StringArray& notes, Heard& mix, std::vector<QualityCheck>& checks) const;          // listen, put right, listen again
    Preset randomLayering (const Preset& original, int layers, juce::Random& rng) const;      // what MUTATE is measured against
    // The experiment's control, and nothing the instrument itself uses: while it is on, candidates() offers every instrument
    // in no order (no likeness, no fit), so a mutation differs from a real one only in how its layers were chosen.
    void chooseBlind (bool on, juce::int64 seed = 1);
    int mainLayer (const Preset& p) const;                                   // the layer that carries the sound (-1: none)
    static juce::String partOf (const Instrument& inst);                     // what part an instrument is suited to on its own evidence
    static juce::StringArray checkNames();                                   // what judge() listens for, in its order
    float ownRing (const Preset& p, const Heard* mainAlone) const;           // the sharpest ring any of its instruments has on its own
    static float ringAllowed (float own, bool holds);                        // how sharp a band may stand, given that ring (a struck sound is given more room than a held one)
    // instruments by likeness to one (Samplebrain's distance): those whose place in the queue lies between lo and hi
    // (0 = most like it, 1 = least), nearest first
    std::vector<Instrument> byLikeness (const Instrument& like, float lo, float hi) const;
    void autoMacros (Preset& p) const;                                       // aims the four macros at what this sound has
    // A sound written by hand, made ready. Each layer's level is set from its measured loudness and the part it plays
    // (the level written in the sound is a trim on top of that), pitched layers are brought onto their key (what the
    // sound wrote as fine tuning stays, as a detune on top), then it is listened to and put right like a mutation, and
    // brought to one loudness (the loudest 400 ms of a chord at targetDb, short of clipping). False: it fails a check.
    // setAside: checks (by name) that do not apply to this kind of sound - a kit of spoken phrases is not "even across the
    // keys" and is not meant to be - and are not held against it.
    bool polish (Preset& p, juce::StringArray& notes, std::vector<QualityCheck>& checks, Heard& heard, juce::String& missing, float targetDb = -17.0f,
                 const juce::StringArray& setAside = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace atlas
