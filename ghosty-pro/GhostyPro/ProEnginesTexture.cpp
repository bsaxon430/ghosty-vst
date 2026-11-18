// ENGINES (round 3): TEXTURE - a granular cloud over the last second of what the oscillator is fed (the other OSC or a SOURCE
// wavetable). Own wrapper around the grain player, audio buffer and diffuser of the vendored MIT granular code
// (third_party/eurorack-mit/clouds, see THIRD_PARTY_LICENSES.md); the feedback path is adapted from its processor. Mono, one instance per
// voice and oscillator, a fixed 1 s buffer allocated up front. It keeps the module's 32 kHz inside (grain sizes and densities are
// counted in samples); the wrapper's rate bridge converts to and from the voice's engine rate.
//   HARMONICS = POSITION (how far back the grains start; cubic), TIMBRE = TEXTURE (grain window; the top quarter adds diffusion),
//   MORPH = DENSITY (regular grains: 0 = a few, 1 = 40 overlapping), plus SIZE, PITCH (semitones), FEEDBACK, FREEZE.
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
#include "clouds/dsp/audio_buffer.h"
#include "clouds/dsp/granular_sample_player.h"
#include "clouds/dsp/fx/diffuser.h"
#include "clouds/dsp/parameters.h"
#include "stmlib/dsp/filter.h"
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
#include <vector>

namespace gpro::eng
{
namespace
{
struct TextureCore final : Core
{
    static constexpr double kRate = 32000.0;
    static constexpr int kBlock = 32;                       // (clouds::kMaxBlockSize)
    static constexpr int kBufferSamples = 32768;            // 1.02 s at 32 kHz, 16 bit
    std::vector<std::int16_t> memory;
    std::vector<float> diffuserMemory;
    std::int16_t tail[256] {};
    clouds::AudioBuffer<clouds::RESOLUTION_16_BIT> buffer;
    clouds::GranularSamplePlayer playerObject;               // (inline: built in the core's zeroed memory)
    clouds::GranularSamplePlayer* const player = &playerObject;
    clouds::Diffuser diffuser;
    stmlib::Svf fbFilter;
    clouds::Parameters params {};
    float fb[kBlock] {}, inBuf[kBlock] {}, frames[kBlock * 2] {};
    float freezeLp = 0.0f;

    TextureCore() : memory ((size_t) kBufferSamples + 16, 0), diffuserMemory (2048, 0.0f) { init(); }

    void init()
    {
        std::fill (memory.begin(), memory.end(), (std::int16_t) 0);
        std::fill (diffuserMemory.begin(), diffuserMemory.end(), 0.0f);
        buffer.Init (memory.data(), kBufferSamples, tail);
        player->Init (1, 40);
        diffuser.Init (diffuserMemory.data());
        fbFilter.Init();
        std::fill (fb, fb + kBlock, 0.0f);
        freezeLp = 0.0f;
    }
    void select (int, double) override { init(); }
    void clear() override { init(); }
    double rateFor (int, double) const override { return kRate; }
    int blockFor (double) const override { return kBlock; }
    bool takesInput() const override { return true; }

    void render (const CoreParams& p, const float* in, float* out, int n) override
    {
        stmlib::Random::Seed (rng);
        n = std::min (n, kBlock);
        // feedback with a high pass against low-frequency build-up (as the module's processor does)
        const float feedback = std::clamp (p.feedback, 0.0f, 1.0f);
        ONE_POLE (freezeLp, p.freeze ? 1.0f : 0.0f, 0.0005f * (float) kBlock / 32.0f)
        fbFilter.set_f_q<stmlib::FREQUENCY_FAST> ((20.0f + 100.0f * feedback * feedback) / (float) kRate, 1.0f);
        fbFilter.Process<stmlib::FILTER_MODE_HIGH_PASS> (fb, fb, (size_t) n);
        const float fbGain = feedback * (1.0f - freezeLp);
        for (int i = 0; i < n; ++i)
        {
            const float x = std::clamp (in[i], -1.0f, 1.0f);
            inBuf[i] = x + fbGain * (stmlib::SoftLimit (fbGain * 1.4f * fb[i] + x) - x);
        }
        buffer.WriteFade (inBuf, n, 1, ! p.freeze);

        // POSITION is cubic: the lower half of the knob covers the last 125 ms (a played note is heard at once), the top the whole second
        const float pos = std::clamp (p.h, 0.0f, 1.0f);
        params.position = pos * pos * pos;
        params.size = std::clamp (p.size, 0.0f, 1.0f);
        params.pitch = std::clamp (p.pitch, -24.0f, 24.0f);
        // DENSITY: the regular-grain half of the module's control (its centre is silent): 0 = a few grains, 1 = 40 overlapping
        params.density = 0.45f - 0.45f * std::clamp (p.m, 0.0f, 1.0f);
        params.texture = std::clamp (p.t, 0.0f, 1.0f);
        params.feedback = feedback; params.freeze = p.freeze; params.trigger = false; params.gate = false;
        params.dry_wet = 1.0f; params.stereo_spread = 0.0f; params.reverb = 0.0f;
        params.granular.use_deterministic_seed = params.density < 0.5f;
        params.granular.overlap = params.density >= 0.53f ? (params.density - 0.53f) * 2.12f : params.density <= 0.47f ? (0.47f - params.density) * 2.12f : 0.0f;
        params.granular.window_shape = params.texture < 0.75f ? params.texture * 1.333f : 1.0f;
        params.granular.stereo_spread = 0.0f;
        player->Play (&buffer, params, frames, (size_t) n);

        const float diffusion = params.texture > 0.75f ? (params.texture - 0.75f) * 4.0f : 0.0f;
        diffuser.set_amount (diffusion);
        diffuser.Process (reinterpret_cast<clouds::FloatFrame*> (frames), (size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float y = 0.5f * (frames[2 * i] + frames[2 * i + 1]);
            fb[i] = y;
            out[i] = std::isfinite (y) ? y : 0.0f;
        }
        rng = stmlib::Random::state();
    }
};
} // namespace

std::unique_ptr<Core> makeTextureCore() { return std::make_unique<TextureCore>(); }
} // namespace gpro::eng
