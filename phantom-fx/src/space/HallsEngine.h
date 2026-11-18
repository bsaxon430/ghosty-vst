// SPDX-License-Identifier: AGPL-3.0-or-later
// HAUNTED HALLS' engine boundary. The engines are Dragonfly Reverb's (Michael Willis, GPL-3.0) on its bundled freeverb3 (Teru Kamogashira,
// GPL-2.0-or-later): the early-reflection model and the Hall (zrev2), Room (progenitor2) and Plate (strev) late reverbs, set up the way
// Dragonfly's own plugins set them up. freeverb3 is pre-C++17 code (dynamic exception specifications), so it is compiled on its own in
// C++14 (src/space/HallsEngine.cpp) behind this plain interface.
#pragma once

namespace phantom { namespace halls {
enum Mode { Room, Hall, Plate, Chamber, Cathedral, Cloud, Galaxy, Reverse, Gated, Nonlinear, DarkTape, kModes };
struct Set;                                    // one configured engine set (early + the mode's late reverb)
Set* create();
void destroy (Set*);
// allocating (worker / prepare thread): sample rate, mode, size 0..1, modulation 0..1 (Dragonfly's wander allocates)
void configure (Set*, double sampleRate, int mode, float size, float mod);
// cheap, the audio thread: decay s, diffusion / damping / width / early 0..1, low cut Hz
void live (Set*, float decay, float diffusion, float damp, float lowcutHz, float width, float earlySend, float mod);
void process (Set*, const float* inL, const float* inR, float* earlyL, float* earlyR, float* lateL, float* lateR, int n);
void mute (Set*);
int mode (const Set*);
}} // namespace phantom::halls
