// SPDX-License-Identifier: GPL-3.0-or-later
// PHANTOM FX SHIM (not DPF): the few declarations of DISTRHO Plugin Framework's DistrhoPlugin.hpp that AnClark's Classic Reverb RE-04 and
// Classic Master Limiter RE-01 DSP files use (the Plugin base with its sample rate and latency, Parameter / State with their hints and
// ranges, and DPF's helper macros), so their code compiles unchanged inside Phantom FX. Phantom FX hosts them through
// src/engines/ClassicEngines.h; nothing here talks to a plugin host.
#pragma once
#ifndef PHANTOM_DPF_HOST
#include "DistrhoPluginInfo.h"   // (the plugin's own, as DPF includes it; not in Phantom FX's host side)
#endif
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <algorithm>

#define START_NAMESPACE_DISTRHO namespace DISTRHO {
#define END_NAMESPACE_DISTRHO }
#define USE_NAMESPACE_DISTRHO using namespace DISTRHO;
#define DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(C) C (const C&) = delete; C& operator= (const C&) = delete;
#define DISTRHO_SAFE_ASSERT_RETURN(cond, ret) if (! (cond)) return ret;
#define DISTRHO_SAFE_ASSERT(cond)

namespace DISTRHO
{
using String = std::string;
static constexpr uint32_t kParameterIsAutomatable = 0x01, kParameterIsBoolean = 0x02, kParameterIsInteger = 0x04, kParameterIsLogarithmic = 0x08, kParameterIsOutput = 0x10;
static constexpr uint32_t kStateIsHostWritable = 0x02;
struct ParameterRanges { float def = 0.0f, min = 0.0f, max = 1.0f; constexpr ParameterRanges() = default; constexpr ParameterRanges (float d, float mn, float mx) : def (d), min (mn), max (mx) {} };
struct Parameter { uint32_t hints = 0; String name, shortName, symbol, unit; ParameterRanges ranges; };
struct State { uint32_t hints = 0; String key, defaultValue, label; };
inline constexpr uint32_t d_version (uint32_t a, uint32_t b, uint32_t c) { return (a << 16) | (b << 8) | c; }
inline constexpr int64_t d_cconst (uint8_t a, uint8_t b, uint8_t c, uint8_t d) { return (a << 24) | (b << 16) | (c << 8) | d; }

class Plugin
{
public:
    Plugin (uint32_t parameterCount, uint32_t programCount, uint32_t stateCount) : nParams (parameterCount) { (void) programCount; (void) stateCount; }
    virtual ~Plugin() = default;
    double getSampleRate() const noexcept { return sampleRate; }
    uint32_t getBufferSize() const noexcept { return 4096; }
    void setLatency (uint32_t frames) noexcept { latency = frames; }
    // (Phantom FX's host side)
    void phantomSetSampleRate (double sr) { sampleRate = sr; sampleRateChanged (sr); activate(); }
    uint32_t phantomLatency() const noexcept { return latency; }
    void phantomRun (const float** in, float** out, uint32_t n) { run (in, out, n); }
    void phantomSet (uint32_t i, float v) { setParameterValue (i, v); }
    float phantomGet (uint32_t i) const { return getParameterValue (i); }
protected:
    virtual const char* getLabel() const { return ""; }
    virtual const char* getMaker() const { return ""; }
    virtual const char* getLicense() const { return ""; }
    virtual uint32_t getVersion() const { return 0; }
    virtual int64_t getUniqueId() const { return 0; }
    virtual void initParameter (uint32_t, Parameter&) {}
    virtual float getParameterValue (uint32_t) const { return 0.0f; }
    virtual void setParameterValue (uint32_t, float) {}
    virtual void initState (uint32_t, State&) {}
    virtual void setState (const char*, const char*) {}
    virtual void activate() {}
    virtual void deactivate() {}
    virtual void sampleRateChanged (double) {}
    virtual void run (const float** inputs, float** outputs, uint32_t frames) = 0;
private:
    uint32_t nParams = 0, latency = 0; double sampleRate = 48000.0;
};
Plugin* createPlugin();
} // namespace DISTRHO
// as DPF does (DistrhoUtils.hpp): plugin code is written against the names without the namespace
#if ! defined (DONT_SET_USING_DISTRHO_NAMESPACE) && ! defined (PHANTOM_DPF_HOST)
using namespace DISTRHO;
#endif
