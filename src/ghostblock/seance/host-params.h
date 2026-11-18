/*
 * SEANCE (GhostBlock) - the handful of Seance's own controls the host can automate, after the engine's, the
 * crossroads', the FX rack's and SUB's: the STEPS sequencer's switches and the output limiter. Fixed ids from 920001.
 * JUCE-free. get / set are safe from any thread (they only touch atomics).
 */
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clap/clap.h>
#include "ghostblock/seance/steps.h"
#include "ghostblock/seance/out-guard.h"

namespace ghostblock::seance
{
struct HostParams
{
    enum : uint32_t { pStepsOn = 920001, pStepsSpeed, pStepsLength, pStepsSwing, pStepsOctave, pStepsLatch, pLimiter, pEnd };
    static constexpr uint32_t kParams = pEnd - pStepsOn;
    static bool isParam(uint32_t id) { return id >= pStepsOn && id < pEnd; }
    struct Def { const char *name; double lo, hi, def; bool stepped; };
    static const Def &def(uint32_t id)
    {
        static const Def d[kParams] = {{"Steps On", 0, 1, 0, true},     {"Steps Speed", 0, 2, 1, true},  {"Steps Length", 0.1, 1.5, 1, false}, {"Steps Swing", 0, 0.75, 0, false},
                                       {"Steps Octave", -2, 2, 0, true}, {"Steps Latch", 0, 1, 0, true}, {"Output Limiter", 0, 1, 1, true}};
        return d[(id - pStepsOn) % kParams];
    }
    static bool info(uint32_t index, clap_param_info *i)
    {
        if (index >= kParams) return false;
        const uint32_t id = pStepsOn + index;
        const auto &d = def(id);
        std::memset(i, 0, sizeof(*i));
        i->id = id;
        i->flags = CLAP_PARAM_IS_AUTOMATABLE | (d.stepped ? CLAP_PARAM_IS_STEPPED : 0);
        std::snprintf(i->name, sizeof(i->name), "%s", d.name);
        std::snprintf(i->module, sizeof(i->module), "%s", id == pLimiter ? "Output" : "Steps");
        i->min_value = d.lo; i->max_value = d.hi; i->default_value = d.def;
        return true;
    }
    static double get(uint32_t id, const StepSeq &q, const OutGuard &g)
    {
        switch (id)
        {
        case pStepsOn: return q.on.load() ? 1 : 0;
        case pStepsSpeed: return q.speed.load();
        case pStepsLength: return q.gate.load();
        case pStepsSwing: return q.swing.load();
        case pStepsOctave: return q.octave.load();
        case pStepsLatch: return q.latch.load() ? 1 : 0;
        case pLimiter: return g.on.load() ? 1 : 0;
        }
        return 0;
    }
    static void set(uint32_t id, double v, StepSeq &q, OutGuard &g)
    {
        const auto &d = def(id);
        v = v < d.lo ? d.lo : v > d.hi ? d.hi : v;
        switch (id)
        {
        case pStepsOn: q.on = v > 0.5; break;
        case pStepsSpeed: q.speed = (int)(v + 0.5); break;
        case pStepsLength: q.gate = (float)v; break;
        case pStepsSwing: q.swing = (float)v; break;
        case pStepsOctave: q.octave = (int)(v < 0 ? v - 0.5 : v + 0.5); break;
        case pStepsLatch: q.latch = v > 0.5; break;
        case pLimiter: g.on = v > 0.5; return;
        }
        q.changed.fetch_add(1, std::memory_order_relaxed); // the page re-reads
    }
    static bool toText(uint32_t id, double v, char *out, uint32_t size)
    {
        static const char *sp[3] = {"Half", "As written", "Double"};
        switch (id)
        {
        case pStepsOn: case pStepsLatch: case pLimiter: std::snprintf(out, size, "%s", v > 0.5 ? "On" : "Off"); break;
        case pStepsSpeed: std::snprintf(out, size, "%s", sp[v < 0.5 ? 0 : v < 1.5 ? 1 : 2]); break;
        case pStepsLength: std::snprintf(out, size, "%d %%", (int)(v * 100 + 0.5)); break;
        case pStepsSwing: std::snprintf(out, size, "%d %%", (int)(v / 0.75 * 100 + 0.5)); break;
        case pStepsOctave: std::snprintf(out, size, "%+d", (int)(v < 0 ? v - 0.5 : v + 0.5)); break;
        default: return false;
        }
        return true;
    }
    static bool fromText(uint32_t id, const char *t, double *v)
    {
        if (!t || !v) return false;
        if (std::strcmp(t, "On") == 0) { *v = 1; return true; }
        if (std::strcmp(t, "Off") == 0) { *v = 0; return true; }
        if (id == pStepsSpeed) { *v = t[0] == 'H' ? 0 : t[0] == 'D' ? 2 : 1; return true; }
        double x = std::atof(t);
        if (id == pStepsLength) x /= 100.0;
        if (id == pStepsSwing) x = x / 100.0 * 0.75;
        *v = x;
        return true;
    }
};
} // namespace ghostblock::seance
