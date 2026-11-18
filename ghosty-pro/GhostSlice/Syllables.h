#pragma once
// GHOST SLICE phase 16: syllable detection (GHOST-SLICE-STUDY.md 3.3). Our own implementation after De Jong & Wempe's syllable
// nuclei (Behavior Research Methods 2009, the paper only), extended with spectral change and the pitch tracker's voicing:
//   frames   10 ms hop, 30 ms window: E = 300-3000 Hz band level (dB, smoothed 50 ms), V = voiced probability (the pYIN-style
//            tracker of Analysis.h when given, else a YIN of the frame), SC = mel-spectrum change (n-3 vs n+3), F = spectral flux
//   nuclei   peaks of E: >= the loud median - 2 dB (or >= the floor + 12 dB), prominence >= 3 dB, voiced (V >= 0.5), >= 90 ms
//            apart with a >= 2 dB dip between neighbours (else merged)
//   vowel    around each nucleus: voiced and within 10 dB of its band level (a fine 1 ms band envelope places the edges);
//            nasals / voiced consonants are voiced but weak in the band, so they stay outside the vowel
//   start    after a silence: the attack that ends it (sample accurate); inside continuous sound: the end of the previous vowel
//            when a consonant sits between, else the lowest point of E - 3 SC - 4 F + 2 V between the nuclei
//   end      the next syllable's start, or where the sound stops
// Everything is deterministic (same samples -> same syllables, bit for bit on one build). Any thread except the audio thread.
#include "Analysis.h"

namespace gslice::analysis
{
// (struct Syllable + kSyllableVersion live in Analysis.h: the analysis Result carries the syllables)
std::vector<Syllable> detectSyllables (const float* x, int64 n, double sr, const PitchTrack* pitch = nullptr, const std::atomic<bool>* cancel = nullptr);
// the syllable that overlaps [a, b) most (nullptr = none)
const Syllable* syllableAt (const std::vector<Syllable>& s, int64 a, int64 b);
juce::String digest (const std::vector<Syllable>& s);

} // namespace gslice::analysis
