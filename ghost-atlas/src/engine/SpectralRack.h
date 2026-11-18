// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - SPECTRAL FX: the effect rack of Poltergeist 2's FX page, as one more effect a slot can hold.
//
// The rack is Poltergeist 2's own, unchanged (third_party/spectral/rack, from this repository's poltergeist-2 branch):
// a CHAIN of four places that each take one of Spectral Corruptor's 38 effects (Dirty Octopus, MPL-2.0; its real
// modules on its own STFT engine), and WARP CORE, WarpCore's spectral inversion (ManasWolrd, GPL-3; its real DSP).
// The upstream sources are vendored unmodified in third_party/spectral. Nothing of the sound is ours.
//
// What is ours is this file and its .cpp: the rack shaped like one of Phantom FX's units (phantom::PhantomEffect), so it
// sits in the same slots as every other effect here - on a layer, on a send, on the master - and so Phantom FX can
// take the same unit. The slot's own ON and MIX stand where Poltergeist 2 has the rack's master ON and MIX.
//
// What it costs: Spectral and Partial effects work on a window of sound (128 to 16384 samples), so a rack that holds
// one speaks late by that window less one sample; the slot reports it as its latency. The GENERIC effects (reverb,
// delay, compressor, distortion, utility) and WARP CORE work on samples and cost nothing.
#pragma once
#include "core/PhantomEffect.h"
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <vector>

namespace atlas
{
namespace spectral
{
constexpr int kPlaces = 4;          // effects in the chain
constexpr int kKnobs = 8;           // the controls of one of them a sound keeps (as Poltergeist 2's host does: an effect's first eight)
constexpr int kWarp = 9;            // WARP CORE's own controls

// ---- the controls, by their place in parameters() (their ids never change: a saved sound names them)
enum { iWindow, iShape, iOverlap, iReach, iChainMix, iOrder, iPlaces };
constexpr int kPerPlace = 3 + kKnobs;                                   // effect, on, mix, then its knobs
constexpr int iWarpOn = iPlaces + kPlaces * kPerPlace;                  // then WARP CORE's nine
constexpr int kControls = iWarpOn + 1 + kWarp;
inline int iEffect (int place) { return iPlaces + place * kPerPlace; }
inline int iOn (int place) { return iEffect (place) + 1; }
inline int iMix (int place) { return iEffect (place) + 2; }
inline int iKnob (int place, int k) { return iEffect (place) + 3 + k; }
inline int iWarp (int k) { return iWarpOn + 1 + k; }
juce::String idOf (int index);                                          // "fft", "s1_fx", "s1_p3", "w_pitch" ...

// ---- the effects (Spectral Corruptor's list; 0 is "none"), for the page
enum { cFloat, cWhole, cSwitch, cChoice };
struct Control
{
    juce::String name;                // upstream's own label
    int kind = cFloat;
    float min = 0.0f, max = 1.0f, def = 0.0f;       // in its own units (what the effect is told)
    bool log = false;
    juce::StringArray choices;        // cChoice
};
int numEffects();                                    // 39: none and the 38
juce::String effectName (int effect);                // "Bin Shuffle"
juce::String effectFolder (int effect);              // "DESTRUCTION" ... ("" for none)
juce::StringArray folders();                         // upstream's eight, in its order
bool usesWindow (int effect);                        // a Spectral or Partial effect: it works on the window and costs its length
int numKnobs (int effect);                           // how many of its controls are kept here (up to kKnobs)
Control knob (int effect, int k);
// a knob's value is kept between 0 and 1 (so a saved sound means the same whatever the effect's units are)
float toKept (int effect, int k, float real);
float fromKept (int effect, int k, float kept);
juce::String written (int effect, int k, float real);            // "37 %", "1200 Hz", as upstream writes it
// puts an effect in a place of a sound's rack: the effect, and where its knobs start here (SpectralRack.cpp, kStarts:
// heard at once, a little; upstream starts most effects at an amount of nothing)
void put (std::map<juce::String, float>& values, int place, int effect);
bool warpAvailable();                                // false if this build has no WarpCore code for the machine's processor
int windowSamples (int choice);                      // 128 ... 16384
}

std::unique_ptr<phantom::PhantomEffect> makeSpectralRack();
std::vector<phantom::ParamSpec> spectralRackParams();

// ---- for the engine, which owns the slot the rack sits in (a rack is known by its key, "spectral")
// One control, set from the message thread at once: the rack builds what it asks for there and then, off the audio
// thread, and swaps it in between blocks. Returns true when the rack's latency has to be asked again (the first
// effect that needs the window, the last one gone, another window): the engine then re-asks the slot.
bool spectralSet (phantom::PhantomEffect& rack, int index, float value);
// Now and then, from any thread but the audio thread: frees what the audio thread has handed back and builds anything
// still asked for. Returns true as spectralSet does.
bool spectralService (phantom::PhantomEffect& rack);
// The newest spectrum of the left channel going into the chain's window (before = true) or coming out of it; false when
// there is nothing new or no effect that uses the window is loaded. rate: the sample rate it belongs to.
bool spectralSpectrum (phantom::PhantomEffect& rack, std::vector<float>& magnitudes, double& rate, bool before);
} // namespace atlas
