/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 */
#include "GhostEngine.h"
#include <cstdio>
#include <cstdlib>
#include "SurgeSynthesizer.h"
#include "tinyxml/tinyxml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <random>

namespace ghost
{
namespace
{
// how far the TERRAIN land moves Surge's controls
struct TerrainTune
{
    float macro[n_terrain_targets]{0.7f, 0.8f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f};
    float param[n_terrain_targets]{0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f};
    // how far the land pushes the sculpt (pull, squash, twist, stretch, pinch): gentler than the EASY sculpt
    float gesture[5]{0.6f, 0.6f, 0.8f, 0.8f, 0.8f};
};
const TerrainTune &terrainTune()
{
    static const TerrainTune t;
    return t;
}

// build 9: how far (normalised, 0..1 of the control's own range) the land may push each kind of control, and the ceiling
// its value may reach. swing = what a full-height mountain does; lo/hi = the safe range of the push (MOVEMENT included);
// wander = how far MOVEMENT at 100% wanders it; dip = the land only ever takes it down (levels, sustain, high-pass).
struct KindTune
{
    float swing, lo, hi, wander;
    bool dip;
    float ceil; // the value never goes above max(its own value, ceil)
};
constexpr float kSculptSpread = 0.9f, kSculptGlitch = 0.5f, kSculptGain = 1.5f;
const KindTune &kindTune(int k)
{
    static const KindTune t[n_terrain_kinds] = {
        /* SKIP     */ {0.f, 0.f, 0.f, 0.f, false, 1.f},
        /* LEVEL    */ {0.14f, -0.12f, 0.f, 0.10f, true, 1.f},
        /* CUTOFF   */ {0.16f, -0.14f, 0.07f, 0.12f, false, 1.f},
        /* RESO     */ {0.14f, -0.12f, 0.10f, 0.10f, false, 0.88f},
        /* ATTACK   */ {0.10f, -0.10f, 0.04f, 0.06f, false, 1.f},
        /* RELEASE  */ {0.12f, -0.15f, 0.03f, 0.08f, false, 1.f},
        /* SUSTAIN  */ {0.12f, -0.12f, 0.f, 0.06f, true, 1.f},
        /* TIME     */ {0.12f, -0.12f, 0.12f, 0.10f, false, 1.f},
        /* FEEDBACK */ {0.12f, -0.12f, 0.10f, 0.08f, false, 0.85f},
        /* HPF      */ {0.15f, -0.15f, 0.f, 0.10f, true, 1.f},
        /* GENERIC  */ {0.20f, -0.20f, 0.20f, 0.16f, false, 1.f},
    };
    return t[std::clamp(k, 0, (int)n_terrain_kinds - 1)];
}
bool has(const std::string &s, const char *w) { return s.find(w) != std::string::npos; }
bool endsWith(const std::string &s, const char *w)
{
    const std::string e(w);
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}
std::string lower(const char *c)
{
    std::string s(c ? c : "");
    for (auto &ch : s)
        ch = (char)std::tolower((unsigned char)ch);
    return s;
}
} // namespace

const char *terrainKindName(int k)
{
    static const char *n[] = {"not moved", "level (dips only)", "cutoff", "resonance", "attack", "release", "sustain (dips only)",
                              "time", "feedback", "high-pass (opens down only)", "smooth control"};
    return n[std::clamp(k, 0, (int)n_terrain_kinds - 1)];
}

// Which Surge controls TERRAIN moves, and why each of the others is left alone. Everything that is a smooth continuous
// control moves; what would break the patch or click does not: types and switches, pitch / octave / tuning / key tracking,
// glide, polyphony and play mode, pan, the delay-like times (they bend pitch), the master volume.
TerrainCoverage GhostEngine::classifyForTerrain(const ::Parameter *p)
{
    TerrainCoverage c;
    if (!p)
    {
        c.why = "none";
        return c;
    }
    c.storage = p->get_storage_name();
    c.label = p->get_full_name();
    const std::string sn = lower(p->get_storage_name()), dn = lower(p->get_name());
    auto skip = [&](const char *w) {
        c.kind = TK_SKIP;
        c.why = w;
        return c;
    };
    if (p->valtype != vt_float)
        return skip("a switch or a choice (not continuous)");
    if (p->can_temposync() && p->temposync)
        return skip("tempo-synced (it moves in steps, not smoothly)");
    switch (p->ctrltype)
    {
    case ct_none:
        return skip("no control");
    case ct_dly_fb_clippingmodes:
    case ct_osctype:
    case ct_fxtype:
    case ct_fxbypass:
    case ct_fbconfig:
    case ct_fmconfig:
    case ct_filtertype:
    case ct_filtersubtype:
    case ct_wstype:
    case ct_wt2window:
    case ct_scenemode:
    case ct_scenesel:
    case ct_polymode:
    case ct_polylimit:
    case ct_oscroute:
    case ct_lfotype:
    case ct_lfotrigmode:
    case ct_envmode:
    case ct_character:
    case ct_sineoscmode:
    case ct_ringmod_sineoscmode:
    case ct_sinefmlegacy:
    case ct_osccount:
    case ct_osccount_or_playcount:
    case ct_vocoder_bandcount:
    case ct_flangermode:
    case ct_fxlfowave:
    case ct_fxlfowave_extended:
    case ct_flangervoices:
    case ct_phaser_stages:
    case ct_vocoder_modulator_mode:
    case ct_airwindows_fx:
    case ct_airwindows_param_integral:
    case ct_reson_mode:
    case ct_envtime_delay_line_mode:
    case ct_nimbusmode:
    case ct_nimbusquality:
    case ct_float_toggle:
    case ct_stringosc_excitation_model:
    case ct_twist_engine:
    case ct_ensemble_stages:
    case ct_alias_wave:
    case ct_alias_mask:
    case ct_alias_bits:
    case ct_mscodec:
    case ct_bonsai_sat_mode:
    case ct_bonsai_noise_mode:
    case ct_distortion_waveshape:
    case ct_reverbshape:
        return skip("a type or a mode switch (would break the sound)");
    case ct_bool:
    case ct_bool_relative_switch:
    case ct_bool_link_switch:
    case ct_bool_keytrack:
    case ct_bool_retrigger:
    case ct_bool_unipolar:
    case ct_bool_mute:
    case ct_bool_solo:
        return skip("an on / off switch");
    case ct_pitch_octave:
    case ct_pitch_semi7bp:
    case ct_pitch_semi7bp_absolutable:
    case ct_pitch:
    case ct_pitch_extendable_very_low_minval:
    case ct_pitch4oct:
    case ct_fmratio:
    case ct_fmratio_int:
    case ct_pbdepth:
    case ct_syncpitch:
    case ct_syncpitch_deformable:
    case ct_flangerpitch:
    case ct_freq_shift:
    case ct_freq_ringmod:
    case ct_tape_speed:
    case ct_ensemble_clockrate:
    case ct_midikey:
    case ct_midikey_or_channel:
        return skip("pitch, tuning or key (the notes would go out of tune)");
    case ct_portatime:
        return skip("glide time");
    case ct_delaymodtime:
    case ct_reverbpredelaytime:
    case ct_chorusmodtime:
    case ct_floaty_warp_time:
    case ct_floaty_delay_time:
    case ct_floaty_delay_playrate:
    case ct_convolution_delay:
    case ct_flangerspacing:
        return skip("a delay time (moving it bends the pitch of the echo)");
    default:
        break;
    }
    if (sn == "volume" || sn == "scene_active" || has(sn, "porta") || has(sn, "octave") || has(sn, "keytrack") ||
        has(sn, "splitkey") || has(sn, "playmode") || has(sn, "polylimit") || has(sn, "scenemode") || has(sn, "tune") ||
        has(sn, "pitch") || endsWith(sn, "_pan") || sn == "pan")
        return skip(sn == "volume" ? "the master volume" : "pitch / tuning / glide / play mode / pan");
    // drive is a tone control (the waveshaper and the distortions): it moves both ways, in the generic range
    if (has(dn, "drive") && !has(dn, "level") && !has(dn, "output"))
    {
        c.kind = TK_GENERIC;
        return c;
    }
    // the kinds
    switch (p->ctrltype)
    {
    case ct_decibel:
    case ct_decibel_narrow:
    case ct_decibel_narrow_extendable:
    case ct_decibel_narrow_short_extendable:
    case ct_decibel_extra_narrow:
    case ct_decibel_attenuation:
    case ct_decibel_attenuation_clipper:
    case ct_decibel_attenuation_large:
    case ct_decibel_attenuation_plus12:
    case ct_decibel_extendable:
    case ct_decibel_deactivatable:
    case ct_decibel_narrow_deactivatable:
    case ct_decibel_extra_narrow_deactivatable:
    case ct_amplitude:
    case ct_amplitude_clipper:
    case ct_amplitude_ringmod:
    case ct_sendlevel:
        c.kind = TK_LEVEL;
        return c;
    case ct_freq_hpf:
        c.kind = TK_HPF;
        return c;
    case ct_freq_audible:
    case ct_freq_audible_deactivatable:
    case ct_freq_audible_deactivatable_lp:
    case ct_freq_audible_with_tunability:
    case ct_freq_audible_very_low_minval:
    case ct_freq_audible_fm3_extendable:
        c.kind = TK_CUTOFF;
        return c;
    case ct_freq_audible_deactivatable_hp:
        c.kind = TK_HPF;
        return c;
    case ct_envtime:
    case ct_envtime_deformable:
    case ct_envtime_deactivatable:
    case ct_envtime_lfodecay:
    case ct_envtime_linkable_delay:
        c.kind = has(dn, "attack") ? TK_ATTACK : has(dn, "release") ? TK_RELEASE : TK_TIME;
        return c;
    case ct_reverbtime:
        c.kind = TK_TIME;
        return c;
    case ct_filter_feedback:
    case ct_osc_feedback:
    case ct_osc_feedback_negative:
        c.kind = TK_FEEDBACK;
        return c;
    default:
        break;
    }
    if (has(sn, "level") || has(dn, "gain") || has(dn, "output") || has(dn, "volume") || has(dn, "level"))
        c.kind = TK_LEVEL;
    else if (has(dn, "resonance") || has(sn, "resonance") || dn == "res" || has(dn, " res"))
        c.kind = TK_RESO;
    else if (has(dn, "feedback") || has(sn, "feedback"))
        c.kind = TK_FEEDBACK;
    else if (has(dn, "sustain"))
        c.kind = TK_SUSTAIN;
    else
        c.kind = TK_GENERIC;
    return c;
}

std::vector<TerrainCoverage> GhostEngine::terrainCoverage() const
{
    std::vector<TerrainCoverage> r;
    auto &pp = synth->storage.getPatch().param_ptr;
    for (int i = 0; i < (int)pp.size(); ++i)
    {
        if (!pp[i])
            continue;
        auto c = classifyForTerrain(pp[i]);
        c.id = i;
        r.push_back(std::move(c));
    }
    return r;
}

void GhostEngine::addExt(ExtTarget t)
{
    uint32_t h = 2166136261u; // FNV-1a of the control's name: a repeatable "place on the land" and wander seed
    for (char c : t.name)
        h = (h ^ (uint8_t)c) * 16777619u;
    t.hid = h;
    auto pos = [](uint32_t x) { x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15; return (float)(x & 0xFFFFu) / 65535.f; };
    t.fx1 = pos(h + 1u);
    t.fx2 = pos(h + 2u);
    t.fx3 = pos(h + 3u);
    ext.push_back(std::move(t));
}

// build 10: the same moves as the Surge controls above (land field at three places, MOVEMENT's wandering, SCULPT's spread, the glitch hops),
// applied to the layers' own controls. Audio thread, once per block, after the Surge controls.
void GhostEngine::driveExt(const float *amt, float sculptMag, float glAmt, bool haveField, float mv)
{
    for (auto &e : ext)
    {
        const bool on = e.enabled && e.enabled();
        const float cur = e.get();
        if (!on)
        {
            if (e.applied != 0.f)
            {
                e.set(std::clamp(e.base, 0.f, 1.f)); // the layer is off: its control goes back to where the user left it
                e.applied = e.off = e.glv = 0.f;
            }
            e.lastWritten = e.get();
            e.init = false;
            e.movedNow = 0.f;
            continue;
        }
        if (!e.init)
        {
            e.base = cur;
            e.applied = e.off = e.glv = 0.f;
            e.init = true;
        }
        else if (std::fabs(cur - e.lastWritten) > 1e-6f)
            e.base = std::clamp(cur - e.applied, 0.f, 1.f); // edited elsewhere (the user, the host): new base
        float o = 0.f;
        if (haveField)
        {
            const float f1 = terrain.field(e.fx1), f2 = terrain.field(e.fx2), f3 = terrain.field(e.fx3);
            float f = std::fabs(f1) >= std::fabs(f2) ? f1 : f2;
            f = std::fabs(f) >= std::fabs(f3) ? f : f3;
            o = e.dip ? -std::fabs(f) * e.swing : f * e.swing;
            if (mv > 0.f)
            {
                const float wgt = std::clamp(0.3f + 1.2f * std::fabs(f), 0.f, 1.f);
                const float w = terrain.wander(e.hid);
                o += e.dip ? -(0.5f + 0.5f * w) * mv * wgt * e.wander : w * mv * wgt * e.wander;
            }
        }
        if (sculptMag > 0.f)
        {
            float sp = 0.f;
            for (int g = 0; g < n_gestures; ++g)
            {
                const uint32_t h = (e.hid + (uint32_t)g * 0x9E3779B1u) * 2246822519u;
                const uint32_t hh = h ^ (h >> 15);
                if ((hh & 7u) < 3u)
                    sp += ((hh & 8u) ? 1.f : -1.f) * amt[g];
            }
            sp *= e.swing * kSculptSpread;
            o += e.dip ? -std::fabs(sp) : sp;
        }
        const float tgt = std::clamp(o, e.lo, e.hi);
        float glT = 0.f;
        if (glAmt > 0.f && (((e.hid >> 7) % 100u) < (burstEnv > 0.05f ? 60u : 30u)))
        {
            const float r = moveRandom(e.hid, (uint32_t)glPos, 0x51ED270Bu);
            glT = std::clamp((e.dip ? -(0.5f + 0.5f * r) : r) * glAmt * e.swing * (burstEnv > 0.05f ? 1.2f : 0.7f), e.lo, e.hi);
        }
        e.off += (tgt - e.off) * smoothCoef;
        if (std::fabs(tgt - e.off) < 1e-6f)
            e.off = tgt;
        e.glv += (glT - e.glv) * glCoef;
        if (std::fabs(glT - e.glv) < 1e-6f)
            e.glv = glT;
        const float total = e.off + e.glv;
        if (total == 0.f && e.applied == 0.f)
        {
            e.lastWritten = cur;
            e.movedNow = 0.f;
            continue;
        }
        const float v = std::clamp(e.base + total, 0.f, 1.f);
        if (std::fabs(v - cur) > 1e-7f)
            e.set(v);
        e.lastWritten = e.get();
        e.applied = e.lastWritten - e.base;
        e.movedNow = e.applied;
    }
}

bool GhostEngine::terrainMapped(int id) const
{
    auto *s = active.load(std::memory_order_acquire);
    if (!s)
        return false;
    for (auto &q : s->params)
        if (q.id == id)
            return q.tkind != TK_SKIP;
    return false;
}


const char *gestureName(int g)
{
    static const char *n[] = {"pull", "squash", "twist", "stretch", "pinch"};
    return n[std::clamp(g, 0, (int)n_gestures - 1)];
}
int gestureFromName(const std::string &s)
{
    for (int g = 0; g < n_gestures; ++g)
        if (s == gestureName(g))
            return g;
    return -1;
}

GhostEngine::GhostEngine(SurgeSynthesizer *s) : synth(s)
{
    synth->storage.ghostPatchHooks.onSave = [this](TiXmlElement &p) { onPatchSave(p); };
    synth->storage.ghostPatchHooks.onLoad = [this](TiXmlElement *p) { onPatchLoad(p); };
    appliedById.assign(synth->storage.getPatch().param_ptr.size() + 8, 0.f);
    nMoved = (int)synth->storage.getPatch().param_ptr.size();
    movedFlag.reset(new std::atomic<uint8_t>[(size_t)nMoved]);
    movedAmt.reset(new std::atomic<float>[(size_t)nMoved]);
    for (int i = 0; i < nMoved; ++i)
    {
        movedFlag[(size_t)i].store(0, std::memory_order_relaxed);
        movedAmt[(size_t)i].store(0.f, std::memory_order_relaxed);
    }
    setSampleRate(synth->storage.samplerate > 0 ? synth->storage.samplerate : 48000);
}

GhostEngine::~GhostEngine()
{
    synth->storage.ghostPatchHooks.onSave = nullptr;
    synth->storage.ghostPatchHooks.onLoad = nullptr;
    delete active.exchange(nullptr);
}

void GhostEngine::setSampleRate(double sr)
{
    terrain.setSampleRate(sr);
    // one-pole per 32-sample block, time constant ~35 ms
    smoothCoef = (float)(1.0 - std::exp(-(double)BLOCK_SIZE / (0.035 * sr)));
    glCoef = (float)(1.0 - std::exp(-(double)BLOCK_SIZE / (0.008 * sr))); // build 9: the glitch hops settle in ~8 ms
}

std::unique_ptr<GhostEngine::MapSet>
GhostEngine::build(const std::vector<MapEntry> &m,
                   const std::vector<std::pair<std::string, float>> &shake, bool rebase) const
{
    auto set = std::make_unique<MapSet>();
    std::map<std::string, int> byName;
    auto &pp = synth->storage.getPatch().param_ptr;
    for (int i = 0; i < (int)pp.size(); ++i)
        byName[pp[i]->get_storage_name()] = i;
    std::map<int, size_t> where;
    auto slot = [&](const std::string &n) -> MapSet::P * {
        auto it = byName.find(n);
        if (it == byName.end())
            return nullptr;
        auto *p = pp[it->second];
        if (p->valtype != vt_float)
            return nullptr; // only continuous parameters are sculpted
        auto w = where.find(it->second);
        if (w != where.end())
            return &set->params[w->second];
        MapSet::P q;
        q.id = it->second;
        q.name = n;
        q.base = p->get_value_f01();
        q.lastWritten = q.base;
        set->params.push_back(q);
        where[it->second] = set->params.size() - 1;
        return &set->params.back();
    };
    for (auto &e : m)
    {
        if (e.gesture < 0 || e.gesture >= n_gestures)
            continue;
        if (auto *q = slot(e.param))
        {
            q->depth[e.gesture] += e.depth;
            set->entries.push_back(e);
        }
    }
    for (auto &s : shake)
        if (auto *q = slot(s.first))
            q->shake = std::clamp(s.second, -0.5f, 0.5f);
    // build 7: the TERRAIN land moves these Surge parameters too (0 with a flat land: never written)
    for (int t = 0; t < n_terrain_targets; ++t)
        if (const char *n = terrainTargetParam(t))
            if (auto *q = slot(n))
                q->tdepth[t] = 1.f;
    // build 9: EVERY Surge control is on the list (even the ones that are "not moved" today: an effect or an oscillator type can
    // change what a control is, and a patch load runs this before Surge has set the oscillator and effect control types). Each
    // sits at two places on the TERRAIN field, fixed by its own number so that nothing shifts when a type changes. What kind of
    // control it is (and so whether it moves) is read again by the audio thread whenever its type changes. Nothing is written
    // while the land is flat.
    for (int i = 0; i < (int)pp.size(); ++i)
    {
        if (!pp[i])
            continue;
        MapSet::P *q = nullptr;
        auto w = where.find(i);
        if (w != where.end())
            q = &set->params[w->second];
        else
        {
            MapSet::P z;
            z.id = i;
            z.name = pp[i]->get_storage_name();
            z.base = pp[i]->get_value_f01();
            z.lastWritten = z.base;
            z.ovOnly = true;
            set->params.push_back(z);
            where[i] = set->params.size() - 1;
            q = &set->params.back();
        }
        q->tkind = classifyForTerrain(pp[i]).kind;
        q->ctype = pp[i]->ctrltype;
        q->ts = pp[i]->temposync ? 1 : 0;
        const float k = (float)i;
        auto fr = [](float v) { return v - std::floor(v); };
        q->fx1 = fr(0.12f + k * 0.6180339887f);
        q->fx2 = fr(0.5f + k * 0.7548776662f);
        q->fx3 = fr(0.83f + k * 0.5698402909f);
        q->hid = (uint32_t)i * 2654435761u + 12345u;
    }
    (void)rebase;
    return set;
}

void GhostEngine::publish(std::unique_ptr<MapSet> s)
{
    // carry the smoothed state of parameters that stay mapped, so a re-map never jumps
    if (auto *old = active.load(std::memory_order_acquire))
    {
        for (auto &q : s->params)
            for (auto &o : old->params)
                if (o.id == q.id)
                {
                    q.base = o.base;
                    q.off = o.off;
                    q.applied = o.applied;
                    q.lastWritten = o.lastWritten;
                }
    }
    auto *raw = s.release();
    auto *prev = active.exchange(raw, std::memory_order_acq_rel);
    if (prev)
        retired.emplace_back(std::unique_ptr<MapSet>(prev), blockCount.load());
    reapRetired();
}

void GhostEngine::reapRetired()
{
    auto now = blockCount.load();
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [now](auto &r) { return now >= r.second + 3; }),
                  retired.end());
    // bound memory even if audio is stopped
    while (retired.size() > 64)
        retired.erase(retired.begin());
}

std::vector<MapEntry> GhostEngine::getMapping() const
{
    std::lock_guard<std::mutex> g(setMtx);
    auto *s = active.load(std::memory_order_acquire);
    return s ? s->entries : std::vector<MapEntry>{};
}

void GhostEngine::setMapping(const std::vector<MapEntry> &m, bool rebase)
{
    std::lock_guard<std::mutex> g(setMtx);
    auto *cur = active.load(std::memory_order_acquire);
    std::vector<std::pair<std::string, float>> sh;
    if (cur)
        for (auto &p : cur->params)
            if (p.shake != 0)
                sh.emplace_back(p.name, p.shake);
    publish(build(m, sh, rebase));
}

std::vector<MapEntry> GhostEngine::autoMapping() const
{
    // A mapping built from what this patch actually uses (scene A). Used for patches that come
    // without one (Surge patches, generated sounds).
    auto &patch = synth->storage.getPatch();
    auto &sc = patch.scene[0];
    std::vector<MapEntry> m;
    auto add = [&](int g, const Parameter &p, float d) { m.push_back({g, p.get_storage_name(), d}); };
    // PULL: open the filter, more resonance, more drive into the shaper, more FM
    add(G_PULL, sc.filterunit[0].cutoff, 0.16f);
    add(G_PULL, sc.filterunit[0].resonance, 0.12f);
    add(G_PULL, sc.wsunit.drive, 0.35f);
    if (sc.fm_switch.val.i != 0)
        add(G_PULL, sc.fm_depth, 0.25f);
    // SQUASH: close the filter, heavier sub layer
    add(G_SQUASH, sc.filterunit[0].cutoff, -0.22f);
    add(G_SQUASH, sc.filterunit[0].resonance, -0.08f);
    add(G_SQUASH, sc.level_o1, 0.10f);
    // TWIST: oscillator character (first param of osc 2 / osc 1) and LFO 1 speed
    for (int o : {1, 0})
        if (sc.osc[o].p[0].valtype == vt_float)
        {
            add(G_TWIST, sc.osc[o].p[0], 0.30f);
            break;
        }
    add(G_TWIST, sc.lfo[0].rate, 0.12f);
    // STRETCH: unison/detune spread on the oscillators that have it, osc 2 pitch drift
    for (int o = 0; o < 3; ++o)
        for (int k = 0; k < n_osc_params; ++k)
            if (sc.osc[o].p[k].ctrltype == ct_oscspread ||
                sc.osc[o].p[k].ctrltype == ct_oscspread_bipolar)
                add(G_STRETCH, sc.osc[o].p[k], 0.25f);
    add(G_STRETCH, sc.osc[1].pitch, 0.004f); // a few cents of detune between layers
    // PINCH: shorter amp decay/sustain, more high pass
    add(G_PINCH, sc.adsr[0].d, -0.18f);
    add(G_PINCH, sc.adsr[0].s, -0.30f);
    add(G_PINCH, sc.lowcut, 0.18f);
    return m;
}

std::vector<std::pair<std::string, float>> GhostEngine::getShake() const
{
    std::lock_guard<std::mutex> g(setMtx);
    std::vector<std::pair<std::string, float>> r;
    if (auto *s = active.load())
        for (auto &p : s->params)
            if (p.shake != 0)
                r.emplace_back(p.name, p.shake);
    return r;
}

void GhostEngine::setShake(const std::vector<std::pair<std::string, float>> &offsets)
{
    std::lock_guard<std::mutex> g(setMtx);
    auto *cur = active.load();
    publish(build(cur ? cur->entries : std::vector<MapEntry>{}, offsets, false));
}

std::vector<std::pair<std::string, float>> GhostEngine::shake(float amount, uint32_t seed)
{
    auto prev = getShake();
    std::vector<std::pair<std::string, float>> nw;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    std::lock_guard<std::mutex> g(setMtx);
    auto *cur = active.load();
    if (!cur)
        return prev;
    for (auto &p : cur->params)
    {
        if (p.ovOnly)
            continue; // (the TERRAIN field's controls are not part of the sculpt)
        // bounded: each param moves at most 'amount' x its largest mapped depth (or 0.08)
        float mx = 0.f;
        for (auto d : p.depth)
            mx = std::max(mx, std::fabs(d));
        if (mx == 0)
            mx = 0.08f;
        nw.emplace_back(p.name, u(rng) * std::clamp(amount, 0.f, 1.f) * mx);
    }
    publish(build(cur->entries, nw, false));
    burstReq.store(1.f, std::memory_order_release); // SHAKE: a big glitch burst (stepped hops of Surge's own controls)
    return prev;
}

void GhostEngine::clearShake() { setShake({}); }

std::vector<GhostEngine::Readout> GhostEngine::readout() const
{
    std::vector<Readout> r;
    std::lock_guard<std::mutex> g(setMtx);
    auto *s = active.load();
    if (!s)
        return r;
    auto &pp = synth->storage.getPatch().param_ptr;
    for (auto &q : s->params)
    {
        if (q.id < 0 || q.id >= (int)pp.size() || q.ovOnly)
            continue;
        Parameter tmp = *pp[q.id];
        Readout o;
        o.param = q.name;
        o.label = tmp.get_full_name();
        o.delta01 = q.applied;
        char buf[256];
        tmp.set_value_f01(q.base);
        tmp.get_display(buf);
        o.from = buf;
        tmp.set_value_f01(std::clamp(q.base + q.applied, 0.f, 1.f));
        tmp.get_display(buf);
        o.to = buf;
        r.push_back(o);
    }
    return r;
}

void GhostEngine::postBlock(const float *L, const float *R, int n)
{
    if (bypass.load(std::memory_order_relaxed))
        return;
    // read only: the last output samples for the TERRAIN analyzer
    int w = scopePos.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        scope[(w + i) & (kScope - 1)] = 0.5f * (L[i] + R[i]);
    scopePos.store((w + n) & (kScope - 1), std::memory_order_release);
}

void GhostEngine::preBlock()
{
    if (bypass.load(std::memory_order_relaxed))
        return;
    blockCount.fetch_add(1, std::memory_order_relaxed);
    // TERRAIN: read the land at this moment of the note (it only moves Surge's own controls)
    terrain.setBeatSeconds(0.5 / std::max(0.05, (double)synth->storage.temposyncratio));
    terrain.tick(BLOCK_SIZE);
    auto *s = active.load(std::memory_order_acquire);
    // MORPH first: it sets the morphed controls' values; the sculpt's offsets stay on top
    if (morph.mode() != Morph::M_OFF)
    {
        std::fill(appliedById.begin(), appliedById.end(), 0.f);
        if (s && sculptEnabled.load(std::memory_order_relaxed))
            for (auto &q : s->params)
                if (q.id >= 0 && q.id < (int)appliedById.size())
                    appliedById[(size_t)q.id] = q.applied;
        morph.preBlock(synth, appliedById.data(), (int)appliedById.size());
    }
    if (!s || !sculptEnabled.load(std::memory_order_relaxed))
        return;
    // the land's macro moves go through Surge's own macro modulation input (the saved macro value is
    // never touched; Surge smooths it); kept inside the macro's range
    const auto &tune = terrainTune();
    for (int t = 0; t < n_terrain_targets; ++t)
    {
        const int m = terrainTargetMacro(t);
        if (m < 0)
            continue;
        float d = terrain.target(t) * tune.macro[t];
        if (d > 0.f)
            d = std::min(d, std::max(0.f, 1.f - synth->getMacroParameterTarget01(m)));
        if (d != macroMod[m])
        {
            macroMod[m] = d;
            synth->applyMacroMonophonicModulation(m, d);
        }
    }
    // the TERRAIN land's gestures (0 with a flat land: nothing changes)
    const float L = 1.f;
    const float want[n_gestures] = {std::min(1.f, L * tune.gesture[0] * terrain.gesture(0)),
                                    std::min(1.f, L * tune.gesture[1] * terrain.gesture(1)),
                                    std::clamp(L * tune.gesture[2] * terrain.gesture(2), -1.f, 1.f),
                                    std::min(1.f, L * tune.gesture[3] * terrain.gesture(3)),
                                    std::min(1.f, L * tune.gesture[4] * terrain.gesture(4))};
    // the sculpt moves at a limited rate (a full flick takes about 200 ms)
    float amt[n_gestures];
    {
        const float maxStep = (float)BLOCK_SIZE / (0.2f * std::max(1000.f, synth->storage.samplerate)); // build 8: a full flick takes 200 ms (120 ms zipped resonant 3rd-party patches)
        for (int g = 0; g < n_gestures; ++g)
        {
            if (!slewInit)
                slewAmt[g] = want[g];
            slewAmt[g] += std::clamp(want[g] - slewAmt[g], -maxStep, maxStep);
            amt[g] = slewAmt[g];
        }
        slewInit = true;
    }
    auto &pp = synth->storage.getPatch().param_ptr;
    const float mv = terrain.fieldOn() ? terrain.getMovement() : 0.f;
    const bool haveField = terrain.fieldOn();
    // SCULPT's size (0 at rest) and the glitch it drives; SHAKE adds a burst that dies away over about a second
    float sculptMag = 0.f;
    for (int g = 0; g < n_gestures; ++g)
        sculptMag = std::max(sculptMag, std::fabs(amt[g]));
    {
        const float rq = burstReq.exchange(0.f, std::memory_order_acq_rel);
        const float blockSec = (float)BLOCK_SIZE / std::max(1000.f, synth->storage.samplerate);
        burstEnv = std::max(burstEnv * std::exp(-blockSec / 0.4f), rq);
        if (burstEnv < 0.01f)
            burstEnv = 0.f;
        const double bps = std::max(30.0, (double)synth->time_data.tempo) / 60.0;
        glPos += (double)blockSec * bps * (burstEnv > 0.05f ? 8.0 : 4.0); // 1/32 notes in a burst, 1/16 notes otherwise
        if (glPos > 1.0e6)
            glPos -= 1.0e6;
    }
    const float glAmt = std::clamp(burstEnv + kSculptGlitch * sculptMag, 0.f, 1.f);
    for (auto &q : s->params)
    {
        if (q.id < 0 || q.id >= (int)pp.size())
            continue;
        Parameter *p = pp[q.id];
        if (p->ctrltype != q.ctype || (p->temposync ? 1 : 0) != q.ts)
        {
            // an oscillator / effect / filter type changed (or the patch finished loading): this control may be a different kind now
            q.tkind = classifyForTerrain(p).kind;
            q.ctype = p->ctrltype;
            q.ts = p->temposync ? 1 : 0;
        }
        float cur = p->get_value_f01();
        if (std::fabs(cur - q.lastWritten) > 1e-6f)
            q.base = std::clamp(cur - q.applied, 0.f, 1.f); // edited elsewhere: new base
        float tgt = q.shake;
        for (int g = 0; g < n_gestures; ++g)
            tgt += q.depth[g] * amt[g] * kSculptGain;
        for (int t = 0; t < n_terrain_targets; ++t)
            tgt += q.tdepth[t] * terrain.target(t) * tune.param[t] * L;
        // build 9: the land as a field over every smooth sound control, MOVEMENT's wandering of the same controls, SCULPT's spread
        // over them and its glitch. (a control whose type changed since the map was built, an FX swapped for another, stays put
        // until the next map)
        float ceilV = 1.f, glT = 0.f;
        const bool kinded = q.tkind != TK_SKIP;
        if (kinded)
        {
            const KindTune &kt = kindTune(q.tkind);
            float o = 0.f;
            if (haveField)
            {
                // each control sits at three places on the land: it follows whichever mountain (or valley) is strongest at them
                const float f1 = terrain.field(q.fx1), f2 = terrain.field(q.fx2), f3 = terrain.field(q.fx3);
                float f = std::fabs(f1) >= std::fabs(f2) ? f1 : f2;
                f = std::fabs(f) >= std::fabs(f3) ? f : f3;
                o = kt.dip ? -std::fabs(f) * kt.swing : f * kt.swing;
                if (mv > 0.f)
                {
                    // under a mountain a control wanders more; on flat ground a little
                    const float wgt = std::clamp(0.3f + 1.2f * std::fabs(f), 0.f, 1.f);
                    const float w = terrain.wander(q.hid);
                    o += kt.dip ? -(0.5f + 0.5f * w) * mv * wgt * kt.wander : w * mv * wgt * kt.wander;
                }
            }
            // SCULPT reaches about a third of the controls with each of its five pulls (which ones is fixed per control)
            if (sculptMag > 0.f)
            {
                float e = 0.f;
                for (int g = 0; g < n_gestures; ++g)
                {
                    const uint32_t h = (q.hid + (uint32_t)g * 0x9E3779B1u) * 2246822519u;
                    const uint32_t hh = h ^ (h >> 15);
                    if ((hh & 7u) < 3u)
                        e += ((hh & 8u) ? 1.f : -1.f) * amt[g];
                }
                e *= kt.swing * kSculptSpread;
                o += kt.dip ? -std::fabs(e) : e;
            }
            tgt += std::clamp(o, kt.lo, kt.hi);
            ceilV = kt.ceil;
            // the glitch hops: the mapped controls always, a share of the others (more of them in a burst)
            if (glAmt > 0.f)
            {
                const uint32_t pick = (q.hid >> 7) % 100u;
                if (q.depth[0] != 0.f || q.depth[1] != 0.f || q.depth[2] != 0.f || q.depth[3] != 0.f || q.depth[4] != 0.f ||
                    pick < (burstEnv > 0.05f ? 60u : 30u))
                {
                    const float r = moveRandom(q.hid, (uint32_t)glPos, 0x51ED270Bu);
                    const float gv = kt.dip ? -(0.5f + 0.5f * r) : r;
                    glT = std::clamp(gv * glAmt * kt.swing * (burstEnv > 0.05f ? 1.2f : 0.7f), kt.lo, kt.hi);
                }
            }
        }
        else if (glAmt > 0.f)
        {
            // a sculpt-mapped control that is not on the field list (a pitch drift of a few cents): its own depth is the size
            float mx = 0.f;
            for (auto d : q.depth)
                mx = std::max(mx, std::fabs(d));
            if (mx > 0.f)
                glT = moveRandom(q.hid, (uint32_t)glPos, 0x51ED270Bu) * glAmt * mx * 0.8f;
        }
        q.off += (tgt - q.off) * smoothCoef;
        if (std::fabs(tgt - q.off) < 1e-6f)
            q.off = tgt;
        q.glv += (glT - q.glv) * glCoef;
        if (std::fabs(glT - q.glv) < 1e-6f)
            q.glv = glT;
        const float total = q.off + q.glv;
        if (total == 0.f && q.applied == 0.f)
        {
            // at rest: Surge's value is left exactly as it is (never re-written, never clamped)
            q.lastWritten = cur;
            continue;
        }
        float v = std::clamp(q.base + total, 0.f, 1.f);
        if (ceilV < 1.f && v > ceilV)
            v = std::max(ceilV, std::min(v, q.base)); // (resonance / feedback never pushed past a safe ceiling)
        if (std::fabs(v - cur) > 1e-7f)
        {
            p->set_value_f01(v);
            if (q.id < nMoved)
                movedFlag[(size_t)q.id].store(1, std::memory_order_relaxed);
        }
        q.lastWritten = p->get_value_f01();
        q.applied = q.lastWritten - q.base;
        if (q.id < nMoved)
            movedAmt[(size_t)q.id].store(q.applied, std::memory_order_relaxed);
    }
    if (!ext.empty())
        driveExt(amt, sculptMag, glAmt, haveField, mv); // build 11: SUB
}


void GhostEngine::onPatchSave(TiXmlElement &patch)
{
    // 1. write base (gesture-free) values, so a reload + saved sculpt state recalls the same sound
    std::lock_guard<std::mutex> g(setMtx);
    auto *s = active.load();
    auto &pp = synth->storage.getPatch().param_ptr;
    if (s)
        if (auto *params = TINYXML_SAFE_TO_ELEMENT(patch.FirstChild("parameters")))
            for (auto &q : s->params)
            {
                if (q.id < 0 || q.id >= (int)pp.size())
                    continue;
                // only a parameter the sculpt has actually moved is written back, as its value now minus
                // the sculpt's offset (Surge already wrote it; a base kept from the patch load could be in
                // another control type's range - the oscillators get theirs after the load - and moved
                // the value a little further on every save)
                if (std::fabs(q.applied) < 1e-7f)
                    continue;
                if (auto *el = TINYXML_SAFE_TO_ELEMENT(params->FirstChild(q.name)))
                {
                    Parameter tmp = *pp[q.id];
                    tmp.set_value_f01(std::clamp(pp[q.id]->get_value_f01() - q.applied, 0.f, 1.f));
                    el->SetDoubleAttribute("value", tmp.val.f);
                }
            }
    // 2. the <ghostbass> section
    TiXmlElement gb("ghostbass");
    gb.SetAttribute("version", 1);
    gb.SetAttribute("category", category);
    gb.SetAttribute("tags", tags);
    gb.SetAttribute("sub_osc", subOsc);
    if (layersSave)
    {
        auto ls = layersSave();
        if (!ls.empty())
        {
            TiXmlElement ly("layers");
            ly.SetAttribute("data", ls);
            gb.InsertEndChild(ly);
        }
    }
    TiXmlElement sq("seq");
    sq.SetAttribute("data", seq.serialise());
    gb.InsertEndChild(sq);
    TiXmlElement ar("arp");
    ar.SetAttribute("data", arp.serialise());
    gb.InsertEndChild(ar);
    {
        // TERRAIN: the live landscape, and the page's A / B state when the page has one (a
        // flat landscape with no page state is left out: older presets stay as they were)
        const auto land = terrain.get();
        const auto page = terrain.getPageState();
        if (!(land.empty() && land.grit <= 0.f) || !page.empty())
        {
            TiXmlElement te("terrain");
            te.SetAttribute("land", ghost::terrain::toString(land));
            te.SetDoubleAttribute("move", terrain.getMovement()); // build 9: MOVEMENT
            te.SetAttribute("mrate", terrain.getMoveRate());
            te.SetDoubleAttribute("mhz", terrain.getMoveFreeHz());
            if (!page.empty())
                te.SetAttribute("page", page);
            gb.InsertEndChild(te);
        }
    }
    morph.onPatchSave(gb); // MORPH (only when this sound is a morph)
    if (s)
    {
        for (auto &e : s->entries)
        {
            TiXmlElement me("map");
            me.SetAttribute("g", gestureName(e.gesture));
            me.SetAttribute("p", e.param);
            me.SetDoubleAttribute("d", e.depth);
            gb.InsertEndChild(me);
        }
        for (auto &q : s->params)
            if (q.shake != 0)
            {
                TiXmlElement se("shake");
                se.SetAttribute("p", q.name);
                se.SetDoubleAttribute("o", q.shake);
                gb.InsertEndChild(se);
            }
    }
    patch.InsertEndChild(gb);
}

void GhostEngine::onPatchLoad(TiXmlElement *patch)
{
    std::vector<MapEntry> m;
    std::vector<std::pair<std::string, float>> shake;
    // a sound without a TERRAIN section has flat land (nothing added to the bass)
    ghost::terrain::Terrain land;
    float moveAmt = kMoveDefault, moveHz = 0.5f;
    int moveRateI = kMoveRateDefault;
    TiXmlElement *morphEl = nullptr;
    std::string page;
    category.clear();
    tags.clear();
    subOsc = "a_osc1";
    bool found = false;
    if (patch)
        if (auto *gb = TINYXML_SAFE_TO_ELEMENT(patch->FirstChild("ghostbass")))
        {
            found = true;
            if (gb->Attribute("category"))
                category = gb->Attribute("category");
            if (gb->Attribute("tags"))
                tags = gb->Attribute("tags");
            if (gb->Attribute("sub_osc"))
                subOsc = gb->Attribute("sub_osc");
            for (auto *c = gb->FirstChildElement(); c; c = c->NextSiblingElement())
            {
                std::string v = c->Value();
                double d;
                int i;
                if (v == "layers")
                {
                    if (c->Attribute("data") && layersLoad)
                        layersLoad(c->Attribute("data")); // a sound without <layers> leaves the layers as they are
                }
                else if (v == "map" && c->Attribute("g") && c->Attribute("p"))
                {
                    MapEntry e;
                    e.gesture = gestureFromName(c->Attribute("g"));
                    e.param = c->Attribute("p");
                    if (c->QueryDoubleAttribute("d", &d) == TIXML_SUCCESS)
                        e.depth = (float)std::clamp(d, -1.0, 1.0);
                    if (e.gesture >= 0)
                        m.push_back(e);
                }
                else if (v == "seq" && c->Attribute("data"))
                {
                    // a pattern with no notes (an old preset) never wipes the one you have
                    ghost::Sequencer probe;
                    bool hasNotes = false;
                    if (probe.deserialise(c->Attribute("data")))
                        for (int k = 0; k < Sequencer::kMaxSteps; ++k)
                            hasNotes = hasNotes || probe.get(k).on;
                    if (!seqLocked.load() && hasNotes)
                    {
                        bool was = seq.playing.load();
                        seq.deserialise(c->Attribute("data"));
                        seq.playing.store(was);
                    }
                }
                else if (v == "morph")
                    morphEl = c;
                else if (v == "terrain")
                {
                    if (c->Attribute("land"))
                        ghost::terrain::fromString(c->Attribute("land"), land);
                    if (c->Attribute("page"))
                        page = c->Attribute("page");
                    double dm = 0;
                    if (c->QueryDoubleAttribute("move", &dm) == TIXML_SUCCESS)
                        moveAmt = (float)dm;
                    if (c->QueryDoubleAttribute("mhz", &dm) == TIXML_SUCCESS)
                        moveHz = (float)dm;
                    c->QueryIntAttribute("mrate", &moveRateI);
                }
                else if (v == "arp" && c->Attribute("data"))
                {
                    if (!seqLocked.load())
                        arp.deserialise(c->Attribute("data"));
                }
                else if (v == "shake" && c->Attribute("p") &&
                         c->QueryDoubleAttribute("o", &d) == TIXML_SUCCESS)
                    shake.emplace_back(c->Attribute("p"), (float)d);
            }
        }
    if (!found)
        m = autoMapping();
    // a fresh map: base = the loaded values (no smoothed carry-over from the previous patch)
    {
        std::lock_guard<std::mutex> g(setMtx);
        auto set = build(m, shake, true);
        auto *raw = set.release();
        for (auto &q : raw->params)
        {
            q.off = q.shake;
            q.applied = 0;
            q.lastWritten = q.base;
        }
        auto *prev = active.exchange(raw, std::memory_order_acq_rel);
        if (prev)
            retired.emplace_back(std::unique_ptr<MapSet>(prev), blockCount.load());
        reapRetired();
    }
    terrain.setPageState(page);
    terrain.setMovement(moveAmt);
    terrain.setMoveRate(moveRateI);
    terrain.setMoveFreeHz(moveHz);
    terrain.set(land);
    terrain.bumpStamp(); // the page re-reads the landscape
    morph.onPatchLoad(synth, morphEl); // (no <morph>: the morph is off)

}

} // namespace ghost
