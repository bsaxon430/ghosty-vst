// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX shared DSP: Airwindows plugins (MIT, Chris Johnson; sources vendored unmodified in third_party/airwindows, see THIRD_PARTY.md)
// behind one small interface. Each plugin is compiled in its own translation unit (aw_<Name>.cpp) inside its own namespace, because the
// Airwindows sources each declare the same global names (kParamA, kNumPrograms ...). Parameters are Airwindows' own 0..1 values.
#pragma once
#include <memory>
namespace phantom::aw
{
struct Plugin
{
    virtual ~Plugin() = default;
    virtual void setSampleRate (double sr) = 0;            // call before processing (message thread); state is reset by recreating
    virtual void setParameter (int index, float value01) = 0;   // Airwindows' own parameter, 0..1 (cheap, audio thread safe)
    virtual void process (float* left, float* right, int numSamples) = 0;   // stereo, in place
};
std::unique_ptr<Plugin> makeTube2();
std::unique_ptr<Plugin> makeConsole7Channel();
std::unique_ptr<Plugin> makeDrumSlam();
std::unique_ptr<Plugin> makeDensity();
std::unique_ptr<Plugin> makeFathomFive();
std::unique_ptr<Plugin> makeBassKit();
// PHANTOM FILTER's sampler-style models (the Z series)
std::unique_ptr<Plugin> makeZLowpass2();
std::unique_ptr<Plugin> makeZHighpass2();
std::unique_ptr<Plugin> makeZBandpass2();
std::unique_ptr<Plugin> makeZNotch2();
std::unique_ptr<Plugin> makeZAcidLowpass();
} // namespace phantom::aw
