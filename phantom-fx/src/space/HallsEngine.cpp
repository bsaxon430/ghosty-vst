// SPDX-License-Identifier: AGPL-3.0-or-later
// HAUNTED HALLS' engine set, compiled as C++14 (freeverb3 needs it). The parameter choices follow Dragonfly Reverb's own DSP.cpp for
// its Hall, Room and Plate ("Tank") plugins and their presets; the per-mode sizes are ours.
#include "HallsEngine.h"
#include "freeverb/earlyref.hpp"
#include "freeverb/zrev2.hpp"
#include "freeverb/progenitor2.hpp"
#include "freeverb/strev.hpp"
#include <cmath>
#include <algorithm>

namespace phantom { namespace halls {
struct Set
{
    fv3::earlyref_f early; fv3::zrev2_f hall; fv3::progenitor2_f room; fv3::strev_f plate;
    int mode = Hall; double sr = 48000.0; float earlySend = 0.2f;
};
static int engineOf (int m) { switch (m) { case Room: case Chamber: case Gated: return 1; case Plate: case DarkTape: return 2; case Nonlinear: return 3; default: return 0; } }   // 0 hall, 1 room, 2 plate, 3 early only
static float metres (int m, float s)
{
    switch (m)
    {
        case Room: return 8.0f + 14.0f * s;   case Chamber: return 14.0f + 16.0f * s; case Hall: return 15.0f + 35.0f * s;
        case Cathedral: return 35.0f + 25.0f * s; case Cloud: return 30.0f + 30.0f * s; case Galaxy: return 40.0f + 20.0f * s;
        case Reverse: return 20.0f + 30.0f * s; case Gated: return 10.0f + 16.0f * s; case Nonlinear: return 12.0f + 28.0f * s;
        default: return 10.0f + 20.0f * s;
    }
}
Set* create() { return new Set(); }
void destroy (Set* s) { delete s; }
void configure (Set* s, double sr, int m, float size, float mod)
{
    s->mode = m; s->sr = sr;
    auto& e = s->early;
    e.setMuteOnChange (false); e.setdryr (0); e.setwet (0); e.setwidth (0.8f); e.setLRDelay (0.3f); e.setLRCrossApFreq (750, 4); e.setDiffusionApFreq (150, 4);
    e.setSampleRate ((float) sr);
    const float mtr = metres (m, size);
    e.setRSFactor (mtr / 10.0f);
    switch (engineOf (m))
    {
        case 0: { auto& l = s->hall; l.setMuteOnChange (false); l.setwet (0); l.setdryr (0); l.setwidth (1.0f); l.setSampleRate ((float) sr);
                  l.setRSFactor (mtr / 80.0f); l.setwander (10.0f + 30.0f * mod);   // mod is fixed at 0.4 by the caller: Dragonfly's Hall wander l.setxover_low (500.0f); l.setxover_high (6000.0f);
                  l.setrt60_factor_low (1.2f); l.setPreDelay (0.1f); l.mute(); break; }
        case 1: { auto& l = s->room; l.setMuteOnChange (false); l.setwet (0); l.setdryr (0); l.setwidth (1.0f); l.setSampleRate ((float) sr);
                  l.setRSFactor (mtr / 10.0f); l.setwander (0.1f + mod * 0.4f); l.setPreDelay (0.1f); l.setbassboost (0.15f); l.setdamp2 (500.0f); l.mute(); break; }
        case 2: { auto& l = s->plate; l.setMuteOnChange (false); l.setdryr (0); l.setwetr (1); l.setdccutfreq (6); l.setspinlimit (12); l.setspindiff (0.15f);
                  l.setSampleRate ((float) sr); l.setRSFactor (0.6f + 0.8f * size); l.setPreDelay (0.1f); l.mute(); break; }
        default: break;
    }
    e.mute();
}
void live (Set* s, float decay, float diffusion, float damp, float lowcut, float width, float earlySend, float mod)
{
    s->earlySend = earlySend;
    const float hiCut = 20000.0f * std::pow (0.1f, damp), md = std::max (0.001f, mod);
    s->early.setoutputlpf (std::min (hiCut * 1.2f, 20000.0f)); s->early.setoutputhpf (lowcut); s->early.setwidth (0.8f * width);
    switch (engineOf (s->mode))
    {
        case 0: { auto& l = s->hall; l.setrt60 (decay); l.setidiffusion1 (diffusion * 0.75f); l.setapfeedback (diffusion * 0.75f); l.setoutputlpf (hiCut); l.setoutputhpf (lowcut);
                  l.setrt60_factor_high (0.25f + 0.6f * (1.0f - damp)); l.setwidth (width); l.setspin (0.5f + 3.0f * mod); l.setspinfactor (md); l.setlfofactor (md); break; }
        case 1: { auto& l = s->room; l.setrt60 (decay); l.setidiffusion1 (diffusion * 0.83f); l.setodiffusion1 (diffusion * 0.83f); l.setdamp (hiCut); l.setoutputdamp (hiCut);
                  l.setwidth (width); l.setspin (0.3f + 2.0f * mod); l.setbassboost (0.15f / std::pow (std::max (0.1f, decay), 1.5f)); break; }
        case 2: { auto& l = s->plate; l.setrt60 (decay); l.setidiffusion1 (0.5f + 0.25f * diffusion); l.setdiffusion1 (0.5f + 0.25f * diffusion); l.setdamp (hiCut); l.setwidth (width);
                  l.setspin (0.5f + 2.5f * mod); break; }
        default: break;
    }
}
void process (Set* s, const float* inL, const float* inR, float* eL, float* eR, float* lL, float* lR, int n)
{
    float tl[512], tr[512];
    for (int o = 0; o < n; o += 512)
    {
        const int k = std::min (512, n - o);
        s->early.processreplace (const_cast<float*> (inL + o), const_cast<float*> (inR + o), eL + o, eR + o, k);
        for (int i = 0; i < k; ++i) { tl[i] = inL[o + i] + s->earlySend * eL[o + i]; tr[i] = inR[o + i] + s->earlySend * eR[o + i]; }
        switch (engineOf (s->mode))
        {
            case 0: s->hall.processreplace (tl, tr, lL + o, lR + o, k); break;
            case 1: s->room.processreplace (tl, tr, lL + o, lR + o, k); break;
            case 2: s->plate.processreplace (tl, tr, lL + o, lR + o, k); break;
            default: std::fill (lL + o, lL + o + k, 0.0f); std::fill (lR + o, lR + o + k, 0.0f); break;
        }
    }
}
void mute (Set* s) { s->early.mute(); switch (engineOf (s->mode)) { case 0: s->hall.mute(); break; case 1: s->room.mute(); break; case 2: s->plate.mute(); break; default: break; } }
int mode (const Set* s) { return s->mode; }
}} // namespace phantom::halls
