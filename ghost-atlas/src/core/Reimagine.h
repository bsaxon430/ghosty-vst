// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - REIMAGINE (experimental, offline): a sound rebuilt out of small pieces of other instruments.
//
// This is Samplebrain's own idea and Samplebrain's own engine (Then Try This, GPL-2.0-or-later: its brain and renderer,
// compiled as upstream wrote them): a target sound is cut into blocks, and each block is replaced by the block of the
// source sounds that is most like it. Here it is done note by note, so that what comes out can be played:
//
//   1. the sound as it is now is played at thirteen notes, half an octave apart (its send effects off);
//   2. four instruments far from it in timbre (Samplebrain's distance again) are played at the same notes;
//   3. for each note Samplebrain rebuilds the target out of the sources' blocks - at the same pitch, since they played
//      the same note. The two stereo sides are rebuilt separately, half a block apart, so they differ;
//   4. the result is given the original's loudness shape, looped where the original held, written as recordings with an
//      SFZ beside them in Library/reimagined/, and listened to and added to the library like any other instrument;
//   5. a sound is made that plays it, as loud as the original, and judged by the same checks as MUTATE's results.
//
// It never runs on the audio thread and nothing of it runs while playing: what it makes is an ordinary instrument.
#pragma once
#include "Mutate.h"

namespace atlas
{
Mutation reimagine (Library& library, const Engine::Resolver& resolver, const Preset& original, juce::uint32 seed, const Progress& progress = {});

// takes an instrument REIMAGINE made out of the library again, and its files with it
bool discardReimagined (Library& library, const Mutation& m);
} // namespace atlas
