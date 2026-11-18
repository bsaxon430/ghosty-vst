/*
 * SEANCE (GhostBlock) - BEAST: Surge XT's own oscillators behind a small C door.
 *
 * Surge XT (surge-synthesizer/surge, GPL-3) is built on its own, with its own copies of the libraries it shares
 * with the Six Sines engine, and linked into one object in which only the beast_* names below are visible. So the
 * two engines never see each other's code. Nothing of Surge's DSP is rewritten here: this file only creates Surge's
 * oscillators (spawn_osc), sets Surge's own parameters, and calls Surge's process_block with its FM input.
 */
#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
typedef struct BeastWorld BeastWorld; // Surge's storage (tables, tuning) + one oscillator setup per seat
typedef struct BeastOsc BeastOsc;     // one sounding oscillator (one per voice per seat)

#define BEAST_BLOCK 16  // samples made per beast_osc_render call, at the engine rate
#define BEAST_PARAMS 7  // Surge oscillators have seven parameters
#define BEAST_SLOTS 12

int beast_type_count(void);
const char *beast_type_name(int type);

BeastWorld *beast_world_create(void);
void beast_world_destroy(BeastWorld *);
void beast_world_set_rate(BeastWorld *, double engineSampleRate); // the rate the 16-sample blocks come out at

void beast_slot_set_type(BeastWorld *, int slot, int type); // sets that type's own controls to their defaults
int beast_slot_type(BeastWorld *, int slot);
const char *beast_slot_param_name(BeastWorld *, int slot, int p); // "" = this type does not use that knob
void beast_slot_set_param01(BeastWorld *, int slot, int p, float v01);
float beast_slot_param01(BeastWorld *, int slot, int p);
float beast_slot_param_default01(BeastWorld *, int slot, int p);
int beast_slot_param_movable(BeastWorld *, int slot, int p); // 1 = a continuous knob MOVE can push
void beast_slot_param_display(BeastWorld *, int slot, int p, float v01, char *out, int n);

BeastOsc *beast_osc_create(BeastWorld *);
void beast_osc_destroy(BeastOsc *);
void beast_osc_start(BeastOsc *, int slot, float pitchKey); // a new note (pitch as a MIDI key, fractions allowed)
// 16 samples. fm16: the modulator's signal (about -1 .. 1 per unit of depth), or null; left and right may be the same buffer
// MOVE: for this note only, knob p of its seat is pushed by delta01 (a share of the knob's whole travel, -1 .. 1).
// p < 0 or delta 0 = the seat's own value. Only knobs with a continuous range move (a stepped choice stays put).
void beast_osc_set_move(BeastOsc *, int p, float delta01);
void beast_osc_render(BeastOsc *, float pitchKey, const float *fm16, float fmDepth, float *outL16, float *outR16);
#ifdef __cplusplus
}
#endif
