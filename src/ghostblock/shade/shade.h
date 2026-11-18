/*
 * SEANCE (GhostBlock) - the SHADE species: Vital's wavetable oscillator in a seat.
 *
 * The oscillator is Vital's own code (libs/vital-synthesis, GPL-3, unchanged): its wavetable playback, its spectral
 * morphs, its warps (sync, formant, bend ...) and its unison. This door shows the engine a few plain C functions and
 * keeps Vital's headers to itself. The wavetables are made here by formula; none of Vital's own tables are included.
 * A shade takes phase modulation at audio rate through Vital's own per-sample phase input, so the threads on the
 * table bend it the way they bend a sine spirit.
 */
#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
typedef struct ShadeWorld ShadeWorld; // per seat: its wavetable and its seven knobs (shared by that seat's voices)
typedef struct ShadeOsc ShadeOsc;     // one sounding oscillator (one per voice per seat)
#define SHADE_PARAMS 7
#define SHADE_SLOTS 12
#define SHADE_MAX_BLOCK 64

int shade_table_count(void);
const char *shade_table_name(int table);
const char *shade_param_name(int p);                             // TABLE, FRAME, MORPH TYPE, MORPH, WARP TYPE, WARP, UNISON
float shade_param_default01(int p);
int shade_param_steps(int p);                                    // 0 = smooth, else how many positions
void shade_param_display(int p, float v01, char *out, int n);
int shade_param_live(int p, const float *all7);                  // 0 = this knob does nothing with the others as they are

ShadeWorld *shade_world_create(void);
void shade_world_destroy(ShadeWorld *);
void shade_world_set_rate(ShadeWorld *, double engineSampleRate);
void shade_slot_set_params01(ShadeWorld *, int slot, const float *all7); // the audio thread, before voices render

// A seat's own table: a Vital wavetable file (.vitaltable, its JSON text), built with Vital's own WavetableCreator.
// Main thread only (as in Vital, where the interface thread loads tables while the audio thread plays them).
// Returns the number of frames, or 0 if the text is not a table (the seat then goes back to the built-in tables).
int shade_slot_load_vitaltable(ShadeWorld *, int slot, const char *json, size_t len);
void shade_slot_clear_user_table(ShadeWorld *, int slot); // back to the built-in tables (the TABLE knob)
int shade_slot_user_frames(ShadeWorld *, int slot);       // 0 = this seat is on a built-in table
unsigned long long shade_slot_user_tag(ShadeWorld *, int slot); // what the caller last passed to ..._set_user_tag
void shade_slot_set_user_tag(ShadeWorld *, int slot, unsigned long long tag);

// A .wav wavetable (one cycle after another; 2048 samples a cycle unless the file's own "clm " note says otherwise)
// rewritten as the text of a Vital wavetable file, so it loads, saves and plays like any other table. Returns
// malloc'd text (free with shade_text_free) or null when the bytes are not a usable wav. frames: how many cycles.
char *shade_wav_to_vitaltable(const void *wav, size_t bytes, const char *name, int *frames);
void shade_text_free(char *);

ShadeOsc *shade_osc_create(ShadeWorld *, int slot);
void shade_osc_destroy(ShadeOsc *);
void shade_osc_start(ShadeOsc *, float pitchKey); // a new note (pitch as a MIDI key, fractions allowed)
// n <= SHADE_MAX_BLOCK samples at the engine rate. pmCycles: phase offset per sample in cycles (null = none).
// move3: for this voice only, added to the seat's FRAME, MORPH and WARP knobs (each -1 .. 1 of the knob's travel).
void shade_osc_render(ShadeOsc *, float pitchKey, const float *pmCycles, const float *move3, float *out, int n);
#ifdef __cplusplus
}
#endif
