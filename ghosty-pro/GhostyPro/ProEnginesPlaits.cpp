// ENGINES (round 3): host for the macro-oscillator engines of the vendored MIT DSP (third_party/eurorack-mit/plaits, see
// THIRD_PARTY_LICENSES.md). Own wrapper: one engine object at a time, placement-constructed in storage owned by the voice, its
// working memory carved out of a buffer allocated up front - no allocation on the audio thread.
// The engines run at the voice's engine rate (the patched dsp.h makes the module's 48 kHz a per-thread runtime value): every pitch,
// filter and time constant that goes through kSampleRate / a0 follows it; the few literals that did not are patched (PATCHES.md).
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
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/speech/naive_speech_synth.h"
#include "plaits/dsp/speech/sam_speech_synth.h"
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
#include <new>
#include <vector>

namespace gpro::eng
{
namespace
{
// SPEECH: our own engine on the formant and phoneme synthesisers of the vendored code (adapted from its speech engine, MIT), without
// its LPC word banks (TI ROM data, not vendored). HARMONICS blends the formant vowel into the phoneme voice, TIMBRE moves the formants
// (species), MORPH picks the vowel; a note-on says a short consonant.
class SpeechVoice final : public plaits::Engine
{
public:
    void Init (stmlib::BufferAllocator* a) override { naive.Init(); sam.Init(); t0 = a->Allocate<float> (plaits::kMaxBlockSize); t1 = a->Allocate<float> (plaits::kMaxBlockSize); }
    void Reset() override {}
    void LoadUserData (const uint8_t*) override {}
    void Render (const plaits::EngineParameters& p, float* out, float* aux, size_t size, bool* enveloped) override
    {
        *enveloped = false;
        const float f0 = plaits::NoteToFrequency (p.note);
        const bool click = p.trigger == plaits::TRIGGER_RISING_EDGE;
        naive.Render (click, f0, p.morph, p.timbre, t0, aux, out, size);
        sam.Render (click, f0, p.morph, p.timbre, t0, t1, size);
        float blend = std::clamp (p.harmonics, 0.0f, 1.0f);
        blend *= blend * (3.0f - 2.0f * blend);
        blend *= blend * (3.0f - 2.0f * blend);
        for (size_t i = 0; i < size; ++i) out[i] += (t1[i] - out[i]) * blend;
    }
private:
    plaits::NaiveSpeechSynth naive;
    plaits::SAMSpeechSynth sam;
    float* t0 = nullptr; float* t1 = nullptr;
};

template <typename... T> constexpr std::size_t maxSize() { std::size_t m = 0; ((m = std::max (m, sizeof (T))), ...); return m; }
constexpr std::size_t kEngineBytes = maxSize<plaits::VirtualAnalogEngine, plaits::WaveshapingEngine, plaits::FMEngine, plaits::AdditiveEngine, plaits::GrainEngine,
                                              plaits::SwarmEngine, plaits::StringEngine, plaits::ModalEngine, plaits::ChordEngine, SpeechVoice, plaits::ParticleEngine>();
constexpr std::size_t kWorkBytes = 80 * 1024;       // the engines' BufferAllocator memory (STRING, the largest, asks for about 62 kB)

struct PlaitsCore final : Core
{
    alignas (16) unsigned char storage[kEngineBytes];
    plaits::Engine* engine = nullptr;
    int id = -1; float rate = 48000.0f; std::size_t workUsed = 0;
    std::vector<unsigned char> work;
    float aux[64] {};

    PlaitsCore() : work (kWorkBytes, 0) {}
    ~PlaitsCore() override { destroy(); }

    // CRASH FIX (installer #23, Mac): plaits::Engine is abstract with a NON-virtual destructor. `engine->~Engine()` is undefined
    // behaviour, and Apple clang (Xcode, Release + LTO) compiled the whole "engine != nullptr" branch of select() to a trap (ud2 at
    // AU image offset 0x499AFB): the second select() of a voice - the next note-on, or a switch to another engine - killed the host.
    // GCC / Linux clang compiled it to nothing. Each engine is now destroyed through its own (concrete) type.
    void (*kill) (plaits::Engine*) noexcept = nullptr;
    void destroy() noexcept
    {
        if (engine != nullptr && kill != nullptr) kill (engine);
        engine = nullptr; kill = nullptr;
    }
    template <typename T> void build()
    {
        static_assert (sizeof (T) <= kEngineBytes && alignof (T) <= 16, "engine storage too small");
        engine = new (storage) T();
        kill = [] (plaits::Engine* p) noexcept { static_cast<T*> (p)->T::~T(); };   // (the exact type placed here: no virtual call needed)
    }

    void select (int e, double r) override
    {
        destroy();
        std::memset (storage, 0, sizeof (storage));          // (the module code expects zeroed memory: see Core::operator new)
        id = e; rate = (float) r;
        switch (e)
        {
            case 0:  build<plaits::VirtualAnalogEngine>(); break;
            case 1:  build<plaits::WaveshapingEngine>(); break;
            case 2:  build<plaits::FMEngine>(); break;
            case 3:  build<plaits::AdditiveEngine>(); break;
            case 4:  build<plaits::GrainEngine>(); break;
            case 5:  build<plaits::SwarmEngine>(); break;
            case 6:  build<plaits::StringEngine>(); break;
            case 7:  build<plaits::ModalEngine>(); break;
            case 8:  build<plaits::ChordEngine>(); break;
            case 9:  build<SpeechVoice>(); break;
            case 10: build<plaits::ParticleEngine>(); break;
            default: engine = nullptr; return;
        }
        std::fill (work.begin(), work.end(), (unsigned char) 0);
        stmlib::BufferAllocator a (work.data(), work.size());
        plaits::SetSampleRate (rate);
        engine->Init (&a);
        engine->Reset();
        workUsed = work.size() - a.free();
    }
    void clear() override { if (id >= 0) select (id, rate); }
    // the engines built on 2-point BLEP oscillators alias at about -45 dB around C5 at 48 kHz: below 96 kHz they run at twice the
    // engine rate and the bridge (a windowed-sinc decimator) brings them down
    double rateFor (int e, double engineRate) const override
    {
        // PARTICLE keeps the module's 48 kHz: its impulse density, filters and diffuser are tuned per sample (noise by design)
        if (e == 10) return 48000.0;
        // CHORD: its divide-down organ stacks 2-point-BLEP saws at 8 x the note (-35 dB alias at C3 at 48 kHz): always at least 4x 44.1 kHz
        if (e == 8) { double r = engineRate; while (r < 170000.0) r *= 2.0; return r; }
        const bool blep = e == 0;
        return blep && engineRate < 90000.0 ? engineRate * 2.0 : engineRate;
    }
    int blockFor (double rate) const override { return std::clamp ((int) std::lround (12.0 * rate / 48000.0), 1, 48); }
    bool takesInput() const override { return false; }

    void render (const CoreParams& p, const float*, float* out, int n) override
    {
        if (engine == nullptr) { std::fill (out, out + n, 0.0f); return; }
        plaits::SetSampleRate (p.rate);
        stmlib::Random::Seed (rng);
        plaits::EngineParameters ep;
        ep.trigger = p.trigger ? plaits::TRIGGER_RISING_EDGE : plaits::TRIGGER_LOW;
        ep.note = 69.0f + 12.0f * std::log2 (std::max (1.0f, p.hz) / 440.0f);     // NoteToFrequency (note) = hz / rate exactly
        ep.harmonics = std::clamp (p.h, 0.0f, 1.0f);
        ep.timbre = std::clamp (p.t, 0.0f, 1.0f);
        ep.morph = std::clamp (p.m, 0.0f, 1.0f);
        ep.accent = std::clamp (p.accent, 0.0f, 1.0f);
        bool enveloped = false;
        engine->Render (ep, out, aux, (size_t) n, &enveloped);
        rng = stmlib::Random::state();
        for (int i = 0; i < n; ++i) if (! std::isfinite (out[i])) out[i] = 0.0f;
    }
};
} // namespace

std::unique_ptr<Core> makePlaitsCore() { return std::make_unique<PlaitsCore>(); }
} // namespace gpro::eng
