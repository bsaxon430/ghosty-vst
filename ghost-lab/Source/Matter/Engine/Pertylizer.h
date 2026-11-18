// Ghost Matter - AGPL-3.0-only. The C interface of third_party/pertylizer-ffi: Pertylizer's own Rust modules
// (pertyjons/pertylizer @ 3726360, MIT, Per Jonsson), linked as a static library. Parameter names are Pertylizer's.
#pragma once
#include <cstddef>
extern "C"
{
struct GmPanner; struct GmReverb; struct GmEuclid; struct GmTuring;
GmPanner* gm_panner_new (float sr);
void gm_panner_free (GmPanner*);
int gm_panner_set (GmPanner*, const char* name, float v);
void gm_panner_process (GmPanner*, const float* in, float* outL, float* outR, int n);
GmReverb* gm_reverb_new (float sr);
void gm_reverb_free (GmReverb*);
int gm_reverb_set (GmReverb*, const char* name, float v);
void gm_reverb_process (GmReverb*, float* l, float* r, int n);
void gm_reverb_reset (GmReverb*);
GmEuclid* gm_euclid_new (float sr);
void gm_euclid_free (GmEuclid*);
int gm_euclid_set (GmEuclid*, const char* name, float v);
void gm_euclid_process (GmEuclid*, const float* clock, float* gate, float* accent, int n);
GmTuring* gm_turing_new (float sr);
void gm_turing_free (GmTuring*);
int gm_turing_set (GmTuring*, const char* name, float v);
size_t gm_rust_allocs();   // every allocation the Rust side has made (tests)
void gm_turing_process (GmTuring*, const float* clock, float* pitch, float* gate, int n);
}
