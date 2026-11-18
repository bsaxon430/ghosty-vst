// SEANCE (GhostBlock) - BEAST. See beast.h. Only Surge's public engine API is used here.
#include "beast.h"
#include "SurgeStorage.h"
#include "dsp/Oscillator.h"
#include <memory>
#include <cstring>
#include <atomic>
#include <new>

namespace
{
const int kTypes[] = {ot_classic, ot_modern, ot_string, ot_twist, ot_alias, ot_shnoise, ot_FM2, ot_FM3, ot_sine};
const int kTypeCount = (int)(sizeof(kTypes) / sizeof(kTypes[0]));
} // namespace

struct BeastWorld
{
    // a Surge patch has two scenes of three oscillators: six setups per storage, so two storages for twelve seats
    std::unique_ptr<SurgeStorage> storage[2];
    int type[BEAST_SLOTS]{};
    std::atomic<uint32_t> gen[BEAST_SLOTS]{};
    SurgeStorage *st(int slot) { return storage[slot / 6].get(); }
    int scene(int slot) const { return (slot % 6) / 3; }
    OscillatorStorage *osc(int slot) { return &st(slot)->getPatch().scene[scene(slot)].osc[slot % 3]; }
};

struct BeastOsc
{
    BeastWorld *w{nullptr};
    int slot{-1};
    uint32_t gen{0};
    Oscillator *osc{nullptr};
    unsigned char buf alignas(16)[oscillator_buffer_size];
    pdata local[n_scene_params];
    float fm alignas(16)[BLOCK_SIZE_OS];
    int moveP{-1};
    float moveD{0.f};
    void drop()
    {
        if (osc) osc->~Oscillator();
        osc = nullptr;
    }
    void copyParams()
    {
        auto *od = w->osc(slot);
        for (int i = 0; i < n_osc_params; ++i) local[od->p[i].param_id_in_scene].i = od->p[i].val.i;
        if (moveP >= 0 && moveP < n_osc_params && moveP < BEAST_PARAMS && moveD != 0.f)
        {
            auto &q = od->p[moveP];
            if (q.ctrltype != ct_none && q.valtype == vt_float)
            {
                const float lo = q.val_min.f, hi = q.val_max.f;
                const float v = q.val.f + moveD * (hi - lo);
                local[q.param_id_in_scene].f = v < lo ? lo : v > hi ? hi : v;
            }
        }
        local[od->pitch.param_id_in_scene].f = 0.f;
        local[od->octave.param_id_in_scene].i = 0;
    }
    void spawn(float pitch)
    {
        drop();
        gen = w->gen[slot].load();
        memset(local, 0, sizeof(local));
        copyParams();
        osc = spawn_osc(kTypes[w->type[slot]], w->st(slot), w->osc(slot), local, local, buf);
        if (osc) osc->init(pitch, false, true);
    }
};

// the door itself must stay visible when everything else is hidden (the Mac linker refuses to export a hidden name)
#pragma GCC visibility push(default)
extern "C"
{
int beast_type_count(void) { return kTypeCount; }
const char *beast_type_name(int t) { return (t >= 0 && t < kTypeCount) ? osc_type_names[kTypes[t]] : ""; }

BeastWorld *beast_world_create(void)
{
    auto *w = new BeastWorld();
    SurgeStorage::SurgeStorageConfig cfg;
    cfg.createUserDirectory = false;
    cfg.scanWavetableAndPatches = false;
    for (auto &s : w->storage)
    {
        s = std::make_unique<SurgeStorage>(cfg);
        s->setSamplerate(48000.f);
    }
    for (int i = 0; i < BEAST_SLOTS; ++i) beast_slot_set_type(w, i, 0);
    return w;
}
void beast_world_destroy(BeastWorld *w) { delete w; }
void beast_world_set_rate(BeastWorld *w, double r)
{
    // Surge's oscillators run at twice its storage rate (BLOCK_SIZE_OS samples a call): so storage = engine / 2
    for (auto &s : w->storage) s->setSamplerate((float)(r * 0.5));
}

void beast_slot_set_type(BeastWorld *w, int slot, int type)
{
    if (slot < 0 || slot >= BEAST_SLOTS || type < 0 || type >= kTypeCount) return;
    w->type[slot] = type;
    auto *od = w->osc(slot);
    od->type.val.i = kTypes[type];
    unsigned char tmp alignas(16)[oscillator_buffer_size];
    if (auto *o = spawn_osc(kTypes[type], w->st(slot), od, nullptr, nullptr, tmp))
    {
        o->init_ctrltypes(w->scene(slot), slot % 3);
        o->init_default_values();
        o->init_extra_config();
        o->~Oscillator();
    }
    w->gen[slot]++;
}
int beast_slot_type(BeastWorld *w, int slot) { return (slot >= 0 && slot < BEAST_SLOTS) ? w->type[slot] : 0; }
static Parameter *par(BeastWorld *w, int slot, int p)
{
    if (!w || slot < 0 || slot >= BEAST_SLOTS || p < 0 || p >= n_osc_params || p >= BEAST_PARAMS) return nullptr;
    return &w->osc(slot)->p[p];
}
const char *beast_slot_param_name(BeastWorld *w, int slot, int p)
{
    auto *q = par(w, slot, p);
    if (!q || q->ctrltype == ct_none) return "";
    return q->get_name();
}
void beast_slot_set_param01(BeastWorld *w, int slot, int p, float v)
{
    if (auto *q = par(w, slot, p)) q->set_value_f01(v < 0 ? 0 : v > 1 ? 1 : v);
}
float beast_slot_param01(BeastWorld *w, int slot, int p)
{
    auto *q = par(w, slot, p);
    return q ? q->get_value_f01() : 0.f;
}
float beast_slot_param_default01(BeastWorld *w, int slot, int p)
{
    auto *q = par(w, slot, p);
    return q ? q->get_default_value_f01() : 0.f;
}
int beast_slot_param_movable(BeastWorld *w, int slot, int p)
{
    auto *q = par(w, slot, p);
    return q && q->ctrltype != ct_none && q->valtype == vt_float ? 1 : 0;
}
void beast_slot_param_display(BeastWorld *w, int slot, int p, float v, char *out, int n)
{
    if (!out || n <= 0) return;
    out[0] = 0;
    auto *q = par(w, slot, p);
    if (!q) return;
    auto s = q->get_display(true, v < 0 ? 0 : v > 1 ? 1 : v);
    strncpy(out, s.c_str(), (size_t)n - 1);
    out[n - 1] = 0;
}

BeastOsc *beast_osc_create(BeastWorld *w)
{
    auto *o = new BeastOsc();
    o->w = w;
    return o;
}
void beast_osc_destroy(BeastOsc *o)
{
    if (!o) return;
    o->drop();
    delete o;
}
void beast_osc_start(BeastOsc *o, int slot, float pitch)
{
    if (!o || slot < 0 || slot >= BEAST_SLOTS) return;
    o->slot = slot;
    o->moveP = -1;
    o->moveD = 0.f;
    o->spawn(pitch);
}
void beast_osc_set_move(BeastOsc *o, int p, float d)
{
    if (!o) return;
    o->moveP = p;
    o->moveD = d < -1.f ? -1.f : d > 1.f ? 1.f : d;
}
void beast_osc_render(BeastOsc *o, float pitch, const float *fm, float depth, float *L, float *R)
{
    if (!o || o->slot < 0 || !o->osc || o->gen != o->w->gen[o->slot].load())
    {
        if (o && o->slot >= 0) o->spawn(pitch); // the seat's type changed under a held note
        if (!o || !o->osc)
        {
            memset(L, 0, sizeof(float) * BEAST_BLOCK);
            if (R && R != L) memset(R, 0, sizeof(float) * BEAST_BLOCK);
            return;
        }
    }
    o->copyParams();
    const bool useFm = fm != nullptr;
    if (useFm) memcpy(o->fm, fm, sizeof(float) * BLOCK_SIZE_OS);
    o->osc->assign_fm(o->fm);
    const bool stereo = R && R != L;
    o->osc->process_block(pitch, 0.f, stereo, useFm, depth);
    memcpy(L, o->osc->output, sizeof(float) * BEAST_BLOCK);
    if (stereo) memcpy(R, o->osc->outputR, sizeof(float) * BEAST_BLOCK);
}
} // extern "C"
#pragma GCC visibility pop
static_assert(BLOCK_SIZE_OS == BEAST_BLOCK, "Surge must be compiled with SURGE_COMPILE_BLOCK_SIZE=8");
