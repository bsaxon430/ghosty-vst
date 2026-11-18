// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - timbre, measured with Samplebrain's own code (Then Try This, GPL-2.0-or-later).
//
// Samplebrain cuts sound into blocks and describes each with an FFT and an MFCC (12 filters), and tells two blocks
// apart with a distance that blends the two. Ghost Atlas uses exactly those classes (block, FFT, Aquila's Mfcc, window,
// search_params) and that distance (block::compare): six blocks of an instrument's held note are kept in a small file
// written by Samplebrain's own block stream, and two instruments are compared block by block.
// MUTATE uses the distance to shortlist; what makes two sounds work together is judged separately (core/Mutate.cpp).
#pragma once
#include "Common.h"

#include <mutex>

namespace atlas
{
// FFTW's planner is not safe to use from two threads at once, and Samplebrain keeps one FFT scratch per thread: every
// use of its engine (the timbre blocks here, REIMAGINE's mosaic) goes through this one lock.
std::mutex& samplebrainLock();

struct TimbreSummary
{
    std::array<float, 12> mfcc {}, mfccDev {};
    std::array<float, 16> fft {};
    int blocks = 0;
};

class Timbre
{
public:
    static constexpr int kBlockSize = 4096;
    static constexpr int kBlocks = 6;

    static juce::File blockFile (const juce::String& instrumentId);
    // mono: a held note at `fs`. Writes the block file and returns the averaged numbers. False if the sound is too short.
    static bool build (const std::vector<float>& mono, double fs, const juce::File& blockFile, TimbreSummary& out);

    // an in-memory set of block files, for many comparisons
    class Index
    {
    public:
        Index();
        ~Index();
        bool add (const juce::String& instrumentId);            // loads its block file; false if there is none
        bool has (const juce::String& instrumentId) const;
        // Samplebrain's distance, averaged over the six blocks. mfccRatio: 0 = FFT only, 1 = MFCC only (its "ratio");
        // normalised: 0 = as recorded, 1 = level taken out (its "n-ratio"). Returns -1 if either is missing.
        double distance (const juce::String& a, const juce::String& b, float mfccRatio = 0.7f, float normalised = 1.0f) const;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
};
} // namespace atlas
