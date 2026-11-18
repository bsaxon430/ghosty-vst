// SEANCE (GhostBlock) - BEAST left out of this build (no Surge object was given to CMake): the door exists, nothing is behind it.
#include "beast.h"
#include <cstring>
extern "C"
{
int beast_type_count(void) { return 0; }
const char *beast_type_name(int) { return ""; }
BeastWorld *beast_world_create(void) { return nullptr; }
void beast_world_destroy(BeastWorld *) {}
void beast_world_set_rate(BeastWorld *, double) {}
void beast_slot_set_type(BeastWorld *, int, int) {}
int beast_slot_type(BeastWorld *, int) { return 0; }
const char *beast_slot_param_name(BeastWorld *, int, int) { return ""; }
void beast_slot_set_param01(BeastWorld *, int, int, float) {}
float beast_slot_param01(BeastWorld *, int, int) { return 0.f; }
float beast_slot_param_default01(BeastWorld *, int, int) { return 0.f; }
void beast_slot_param_display(BeastWorld *, int, int, float, char *out, int n) { if (out && n > 0) out[0] = 0; }
BeastOsc *beast_osc_create(BeastWorld *) { return nullptr; }
void beast_osc_destroy(BeastOsc *) {}
void beast_osc_start(BeastOsc *, int, float) {}
void beast_osc_render(BeastOsc *, float, const float *, float, float *L, float *R)
{
    memset(L, 0, sizeof(float) * BEAST_BLOCK);
    if (R && R != L) memset(R, 0, sizeof(float) * BEAST_BLOCK);
}
}
