// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: the few VST2 SDK names the Airwindows sources use, so their real code compiles unchanged inside the rack.
// Only the audio path (constructor, setParameter, processReplacing, getSampleRate) is ever called; the rest are no-ops.
#pragma once
#define __audioeffect__ 1
#include <cstdint>
#include <cstring>
#include <cstdio>
typedef int32_t VstInt32;
typedef void* audioMasterCallback;
enum VstPlugCategory { kPlugCategUnknown = 0, kPlugCategEffect };
enum { kVstMaxProgNameLen = 24, kVstMaxParamStrLen = 8, kVstMaxVendorStrLen = 64, kVstMaxProductStrLen = 64, kVstMaxEffectNameLen = 32 };
inline void vst_strncpy (char* dst, const char* src, int maxLen) { std::strncpy (dst, src, (size_t) maxLen); dst[maxLen] = 0; }
inline void float2string (float v, char* text, int maxLen) { std::snprintf (text, (size_t) maxLen + 1, "%.3f", v); }
class AudioEffectX;
typedef AudioEffectX AudioEffect;
class AudioEffectX
{
public:
    AudioEffectX (audioMasterCallback, int, int) {}
    virtual ~AudioEffectX() = default;
    void setNumInputs (int) {} void setNumOutputs (int) {} void setUniqueID (unsigned long) {}
    void canProcessReplacing() {} void canDoubleReplacing() {} void programsAreChunks (bool) {}
    float getSampleRate() const { return phantomSampleRate; }
    void phantomSetSampleRate (double sr) { phantomSampleRate = (float) sr; }
private:
    float phantomSampleRate = 44100.0f;
};
