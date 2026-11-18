// ENGINES (round 3): RESONATOR - a tuned resonator per voice, played by the other oscillator (or a SOURCE wavetable) and, when
// nothing feeds it, plucked by its own exciter at every note. Own wrapper around the modal resonator, string model and plucker of the
// vendored MIT resonator code (third_party/eurorack-mit/rings, see THIRD_PARTY_LICENSES.md); the excitation filter, strike and pluck
// follow its one-voice signal path. It keeps the module's 48 kHz inside (mode decays and string loops are tuned per sample); the
// wrapper's rate bridge converts to and from the voice's engine rate.
//   HARMONICS = STRUCTURE (mode spread / string stiffness), TIMBRE = BRIGHTNESS, MORPH = DAMPING, plus POSITION and MODEL (Modal / String).
#define TEST 1   // (the vendored headers: desktop build, no ARM assembly)
#include "ProEnginesCore.h"
#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wshadow"
#elif defined (_MSC_VER)
 #pragma warning (push, 0)
#endif
#include "rings/dsp/dsp.h"
#include "rings/dsp/resonator.h"
#include "rings/dsp/string.h"
#include "stmlib/utils/random.h"
namespace rings { using namespace stmlib; }     // (plucker.h expects the includer's using-directive, as in the module's part.cc)
#include "rings/dsp/plucker.h"
#include "rings/dsp/limiter.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"
#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#elif defined (_MSC_VER)
 #pragma warning (pop)
#endif
#include <algorithm>
#include <cmath>
#include <memory>

namespace gpro::eng
{
namespace
{
struct ResonatorCore final : Core
{
    static constexpr int kBlock = 24;                       // (rings::kMaxBlockSize)
    struct State
    {
        rings::Resonator modal;
        rings::String string;
        rings::Plucker plucker;
        stmlib::Svf excitation;
        stmlib::DCBlocker dc;
        rings::Limiter limiter;
    };
    State state;                                            // (inline, in the core's zeroed memory; the string's delay lines: ~12 kB)
    State* const s = &state;
    float in[kBlock] {}, out[kBlock] {}, aux[kBlock] {}, burst[kBlock] {};
    int model = -1;

    ResonatorCore() { init (0); }

    void init (int m)
    {
        model = m;
        s->modal.Init(); s->modal.set_resolution (60);
        s->string.Init (true);
        s->plucker.Init();
        s->excitation.Init();
        s->dc.Init (1.0f - 10.0f / rings::kSampleRate);
        s->limiter.Init();
    }
    void select (int, double) override { init (model < 0 ? 0 : model); }
    void clear() override { init (model < 0 ? 0 : model); }
    double rateFor (int, double) const override { return rings::kSampleRate; }
    int blockFor (double) const override { return kBlock; }
    bool takesInput() const override { return true; }

    void render (const CoreParams& p, const float* input, float* o, int n) override
    {
        stmlib::Random::Seed (rng);
        n = std::min (n, kBlock);
        const int m = p.model == 1 ? 1 : 0;
        if (m != model) init (m);
        // (the module's controls stop short of 1: at STRUCTURE = 1 the stiffness table was read one past its end - ASan, installer #24)
        const float structure = std::clamp (p.h, 0.0f, 0.9995f), brightness = std::clamp (p.t, 0.0f, 0.9995f), damping = std::clamp (p.m, 0.0f, 0.995f);
        const float position = std::clamp (p.position, 0.0f, 0.9995f);
        const float frequency = std::clamp (p.hz / rings::kSampleRate, 10.0f / rings::kSampleRate, 0.45f);
        const bool internal = ! p.haveInput;                 // nothing feeds it: it plucks itself at every note
        const float cutoff = brightness * (2.0f - brightness);
        const float range = internal ? frequency * stmlib::SemitonesToRatio ((cutoff - 0.5f) * 96.0f) : 0.4f * stmlib::SemitonesToRatio ((cutoff - 1.0f) * 108.0f);
        const float filterCutoff = std::min (range, 0.499f);
        s->excitation.set_f_q<stmlib::FREQUENCY_DIRTY> (filterCutoff, internal ? 1.5f : 0.8f);
        // (a full-level oscillator rings the resonator about 28 dB louder than its own pluck: -28 dB in, so switching sources does not jump)
        for (int i = 0; i < n; ++i) in[i] = internal ? 0.0f : 0.04f * std::clamp (input[i], -2.0f, 2.0f);
        std::fill (out, out + n, 0.0f); std::fill (aux, aux + n, 0.0f);
        if (model == 0)
        {   // MODAL: strike (internal) -> excitation low pass -> 60 band-pass modes
            if (internal && p.trigger) in[0] += 0.25f * stmlib::SemitonesToRatio (filterCutoff * filterCutoff * 24.0f) / filterCutoff;
            s->excitation.Process<stmlib::FILTER_MODE_LOW_PASS> (in, in, (size_t) n);
            s->modal.set_frequency (frequency); s->modal.set_structure (structure); s->modal.set_brightness (brightness * brightness);
            s->modal.set_position (position); s->modal.set_damping (damping);
            s->modal.Process (in, out, aux, (size_t) n);
        }
        else
        {   // STRING: input -> excitation low pass (+ a noise pluck when internal) -> DC block -> a dispersive string
            for (int i = 0; i < n; ++i) in[i] *= 0.70710678f;
            s->excitation.Process<stmlib::FILTER_MODE_LOW_PASS> (in, in, (size_t) n);
            if (internal)
            {
                if (p.trigger) s->plucker.Trigger (frequency, filterCutoff * 8.0f, position);
                s->plucker.Process (burst, (size_t) n);
                for (int i = 0; i < n; ++i) in[i] += burst[i];
            }
            s->dc.Process (in, (size_t) n);
            const float dispersion = structure < 0.24f ? (structure - 0.24f) * 4.166f : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);
            s->string.set_dispersion (dispersion); s->string.set_frequency (frequency); s->string.set_brightness (brightness);
            s->string.set_position (position); s->string.set_damping (damping);
            s->string.Process (in, out, aux, (size_t) n);
        }
        s->limiter.Process (out, aux, (size_t) n, 1.4f);
        for (int i = 0; i < n; ++i) { const float y = 0.5f * (out[i] + aux[i]); o[i] = std::isfinite (y) ? y : 0.0f; }
        rng = stmlib::Random::state();
    }
};
} // namespace

std::unique_ptr<Core> makeResonatorCore() { return std::make_unique<ResonatorCore>(); }
} // namespace gpro::eng
